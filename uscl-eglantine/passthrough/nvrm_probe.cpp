// Phase 0 probe for the Eglantine GPU passthrough backend: reach the host NVIDIA GPU through the
// RM ioctls the userspace driver itself uses, on our own channel, and run tiny submissions.
// Steps (argv[1]): "alloc" = objects only, "sem" = + one host semaphore release,
// "compute <sass>" = + one compute launch writing a buffer, "clear <sass>" = + an ADA_A surface clear,
// "mthd <offset> <value>" = sem, then one ADA_A method, "barrier <sass> [prog]" = compute, then an empty-grid
// QMD that only releases a semaphore, "inline <sass>" = compute, then the same QMD as an inline QMD.
// Safety: the root client is freed on every exit path (RM frees all children with it).
#include "nvrm.h"

int main(int argc, char** argv) {
    const char* step = argc > 1 ? argv[1] : "alloc";
    rm_open();
    NvHandle vas = g_vas;

    // One 2 MiB vidmem block: GPFIFO ring @0, USERD @0x2000, pushbuffer @0x10000, data @0x100000.
    const NvU64 SZ = 2 << 20, RING = 0, USERD = 0x2000, PB = 0x10000, DATA = 0x100000;
    const NvU32 ENTRIES = 0x400;
    NvHandle mem = alloc_mem(SZ, true);
    auto* cpu = (volatile NvU8*)map_cpu(g_device, mem, SZ, DRF_DEF(OS33, _FLAGS, _CACHING_TYPE, _WRITECOMBINED));
    NvU64 gva = map_gpu(mem, SZ);
    NvHandle notifier = alloc_mem(64 << 10, false);
    printf("vidmem gva 0x%llx\n", (unsigned long long)gva);

    NvHandle usermode = rm_alloc(g_subdevice, AMPERE_USERMODE_A, nullptr, "usermode");
    auto* mmio = (volatile NvU32*)map_cpu(g_device, usermode, 0x10000, 0);

    NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS tp{}; tp.engineType = NV2080_ENGINE_TYPE_GRAPHICS; tp.hVASpace = vas;
    NvHandle tsg = rm_alloc(g_device, KEPLER_CHANNEL_GROUP_A, &tp, "tsg");
    NV_CTXSHARE_ALLOCATION_PARAMETERS cp{}; cp.hVASpace = vas; cp.flags = NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_SYNC; // veid 0: graphics needs it
    NvHandle ctxshare = rm_alloc(tsg, FERMI_CONTEXT_SHARE_A, &cp, "ctxshare");

    NV_CHANNEL_ALLOC_PARAMS ch{};
    ch.hObjectError = notifier; ch.hObjectBuffer = mem; ch.gpFifoOffset = gva + RING; ch.gpFifoEntries = ENTRIES;
    ch.hContextShare = ctxshare; ch.hUserdMemory[0] = mem; ch.userdOffset[0] = USERD;
    NvHandle chan = rm_alloc(tsg, AMPERE_CHANNEL_GPFIFO_A, &ch, "channel");

    NvHandle o3d = rm_alloc(chan, ADA_A, nullptr, "ADA_A");
    NvHandle ocomp = rm_alloc(chan, ADA_COMPUTE_A, nullptr, "ADA_COMPUTE_A");
    NvHandle ocopy = rm_alloc(chan, AMPERE_DMA_COPY_B, nullptr, "AMPERE_DMA_COPY_B");
    printf("objects: 3d 0x%x compute 0x%x copy 0x%x\n", o3d, ocomp, ocopy);

    NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS sch{}; sch.bEnable = NV_TRUE;
    rm_control(tsg, NVA06C_CTRL_CMD_GPFIFO_SCHEDULE, &sch, "schedule");
    NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS tok{}; tok.workSubmitToken = ~0u;
    rm_control(chan, NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN, &tok, "work submit token");
    printf("gate1: channel + ADA_A + ADA_COMPUTE_A + copy allocated, token 0x%x\n", tok.workSubmitToken);
    if (!strcmp(step, "alloc")) return 0;

    // Submission: append methods to the pushbuffer, one GP entry per submit, then a host semaphore release
    // (WFI) as the completion fence, polled from the CPU.
    auto* pbw = (volatile NvU32*)(cpu + PB);
    auto* userd = (volatile AmpereAControlGPFifo*)(cpu + USERD);
    auto* sem = (volatile NvU32*)(cpu + DATA);
    NvU32 pbn = 0, gpput = 0, fence = 0;
    std::vector<NvU32> q;
    auto m = [&](NvU32 subc, NvU32 mthd, std::initializer_list<NvU32> v) {
        q.push_back(hdr(subc, mthd, (NvU32)v.size())); q.insert(q.end(), v);
    };
    auto submit = [&](const char* what) {
        NvU64 sa = gva + DATA;
        m(0, NVC56F_SEM_ADDR_LO, {(NvU32)sa, (NvU32)(sa >> 32), ++fence + 0xC0FFEE00, 0,
            DRF_DEF(C56F, _SEM_EXECUTE, _OPERATION, _RELEASE) | DRF_DEF(C56F, _SEM_EXECUTE, _RELEASE_WFI, _EN) |
            DRF_DEF(C56F, _SEM_EXECUTE, _PAYLOAD_SIZE, _32BIT)});
        NvU64 pa = gva + PB + pbn * 4;
        for (NvU32 w : q) pbw[pbn++] = w;
        ((volatile NvU64*)(cpu + RING))[gpput] = pa | ((NvU64)q.size() << 42); // GP_ENTRY: get, LEVEL_MAIN, LENGTH
        q.clear();
        __sync_synchronize();
        userd->GPPut = ++gpput;
        __sync_synchronize();
        mmio[NVC361_NOTIFY_CHANNEL_PENDING / 4] = tok.workSubmitToken;
        timespec t0, t;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (;;) {
            if (*sem == fence + 0xC0FFEE00) break;
            clock_gettime(CLOCK_MONOTONIC, &t);
            if (t.tv_sec - t0.tv_sec > 2) {
                printf("%s: TIMEOUT, sem 0x%x GPGet %u\n", what, *sem, userd->GPGet);
                exit(3);
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        printf("%s: done after %ld us, GPGet %u\n", what,
               (t.tv_sec - t0.tv_sec) * 1000000 + (t.tv_nsec - t0.tv_nsec) / 1000, userd->GPGet);
    };
    *sem = 0;
    submit("sem");
    if (!strcmp(step, "sem")) return 0;

    // step "mthd <offset> <value>": does ADA_A accept one 3D method (e.g. one missing from the public headers)?
    if (!strcmp(step, "mthd")) {
        if (argc < 4) { fprintf(stderr, "mthd needs <offset> <value>\n"); return 1; }
        NvU32 off = strtoul(argv[2], nullptr, 0), val = strtoul(argv[3], nullptr, 0);
        m(0, NVC997_SET_OBJECT, {ADA_A});
        m(0, off, {val});
        submit("mthd");
        return 0;
    }

    // step "compute": one 32-thread launch of argv[2] (raw SM89 SASS of `p[tid] = 0xA0000000 + tid`,
    // pointer param at c[0][0x160]). QMD v3 settings follow tinygrad's Ada path; field widths from mesa.
    if (argc < 3) { fprintf(stderr, "compute needs the SASS file\n"); return 1; }
    std::vector<NvU8> sass;
    { FILE* f = fopen(argv[2], "rb"); if (!f) die("open sass", 0); int c; while ((c = fgetc(f)) != EOF) sass.push_back(c); fclose(f); }
    NV2080_CTRL_GR_INFO gi[5] = {{NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_GPCS, 0}, {NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_TPC_PER_GPC, 0},
        {NV2080_CTRL_GR_INFO_INDEX_LITTER_NUM_SM_PER_TPC, 0}, {NV2080_CTRL_GR_INFO_INDEX_MAX_WARPS_PER_SM, 0},
        {NV2080_CTRL_GR_INFO_INDEX_SM_VERSION, 0}};
    NV2080_CTRL_GR_GET_INFO_PARAMS gp{}; gp.grInfoListSize = 5; gp.grInfoList = gi;
    rm_control(g_subdevice, NV2080_CTRL_CMD_GR_GET_INFO, &gp, "gr info");
    NvU32 gpcs = gi[0].data, tpcs = gi[1].data, smpt = gi[2].data, warps = gi[3].data, smv = gi[4].data;
    printf("gr: %u GPCs x %u TPCs x %u SMs, %u warps/SM, sm_version 0x%x\n", gpcs, tpcs, smpt, warps, smv);

    const NvU64 SLM = 0x240, LWIN = 0x729300000000ull, SWIN = 0x729400000000ull;
    NvU64 per_tpc = rup(rup(SLM * 32, 0x200) * warps * smpt, 0x8000), lm_sz = rup(per_tpc * tpcs * gpcs, 0x20000);
    NvU64 lm = map_gpu(alloc_mem(lm_sz, true), lm_sz);

    const NvU64 PROG = DATA + 0x10000, QMDO = DATA + 0x20000, CB0 = QMDO + 0x100, OUT = DATA + 0x30000;
    for (size_t i = 0; i < sass.size(); i++) cpu[PROG + i] = sass[i];
    auto* out = (volatile NvU32*)(cpu + OUT);
    for (int i = 0; i < 64; i++) out[i] = 0;
    auto* cb0 = (volatile NvU32*)(cpu + CB0);
    for (int i = 0; i < 0x200 / 4; i++) cb0[i] = 0;
    NvU64 cbv[3] = {SWIN, LWIN, 0xfffdc0};  // driver params at c[0][0x18..0x30)
    for (int i = 0; i < 3; i++) { cb0[6 + 2 * i] = (NvU32)cbv[i]; cb0[7 + 2 * i] = (NvU32)(cbv[i] >> 32); }
    NvU64 outva = gva + OUT;
    cb0[0x160 / 4] = (NvU32)outva; cb0[0x164 / 4] = (NvU32)(outva >> 32);

    NvU32 qmd[64] = {};
    auto qs = [&](int hi, int lo, NvU64 v) {
        for (int b = lo; b <= hi; b++, v >>= 1)
            qmd[b / 32] = (qmd[b / 32] & ~(1u << (b % 32))) | ((NvU32)(v & 1) << (b % 32));
    };
#define MW(r) r
#define Q(f, v) qs(1 ? NVC6C0_QMDV03_00_##f, 0 ? NVC6C0_QMDV03_00_##f, v)
    Q(QMD_MAJOR_VERSION, 3); Q(QMD_VERSION, 0); Q(SM_GLOBAL_CACHING_ENABLE, 1); Q(QMD_GROUP_ID, 0x3f);
    Q(INVALIDATE_TEXTURE_HEADER_CACHE, 1); Q(INVALIDATE_TEXTURE_SAMPLER_CACHE, 1); Q(INVALIDATE_TEXTURE_DATA_CACHE, 1);
    Q(INVALIDATE_SHADER_DATA_CACHE, 1); Q(API_VISIBLE_CALL_LIMIT, 1); Q(SAMPLER_INDEX, 1); Q(BARRIER_COUNT, 1);
    Q(CWD_MEMBAR_TYPE, NVC6C0_QMDV03_00_CWD_MEMBAR_TYPE_L1_SYSMEMBAR);
    Q(SHARED_MEMORY_SIZE, 0x400); Q(MIN_SM_CONFIG_SHARED_MEM_SIZE, 9); Q(TARGET_SM_CONFIG_SHARED_MEM_SIZE, 9);
    Q(MAX_SM_CONFIG_SHARED_MEM_SIZE, 0x1a); Q(REGISTER_COUNT_V, 8); Q(SHADER_LOCAL_MEMORY_HIGH_SIZE, SLM);
    Q(SASS_VERSION, ((smv & 0xf00) >> 4) | (smv & 0xf));
    Q(CTA_RASTER_WIDTH, 1); Q(CTA_RASTER_HEIGHT, 1); Q(CTA_RASTER_DEPTH, 1);
    Q(CTA_THREAD_DIMENSION0, 32); Q(CTA_THREAD_DIMENSION1, 1); Q(CTA_THREAD_DIMENSION2, 1);
    NvU64 pva = gva + PROG, cva = gva + CB0;
    Q(PROGRAM_ADDRESS_LOWER, (NvU32)pva); Q(PROGRAM_ADDRESS_UPPER, pva >> 32);
    Q(PROGRAM_PREFETCH_ADDR_LOWER_SHIFTED, (NvU32)(pva >> 8)); Q(PROGRAM_PREFETCH_ADDR_UPPER_SHIFTED, pva >> 40);
    Q(PROGRAM_PREFETCH_SIZE, std::min<NvU64>(sass.size() >> 8, 0x1ff));
    Q(CONSTANT_BUFFER_VALID(0), 1); Q(CONSTANT_BUFFER_INVALIDATE(0), 1); Q(CONSTANT_BUFFER_SIZE_SHIFTED4(0), 0x200 >> 4);
    Q(CONSTANT_BUFFER_ADDR_LOWER(0), (NvU32)cva); Q(CONSTANT_BUFFER_ADDR_UPPER(0), cva >> 32);
#undef Q
    for (int i = 0; i < 64; i++) ((volatile NvU32*)(cpu + QMDO))[i] = qmd[i];

    NvU64 qva = gva + QMDO;
    m(1, NVC9C0_SET_OBJECT, {ADA_COMPUTE_A});
    m(1, NVC9C0_SET_SHADER_LOCAL_MEMORY_WINDOW_A, {(NvU32)(LWIN >> 32), (NvU32)LWIN});
    m(1, NVC9C0_SET_SHADER_SHARED_MEMORY_WINDOW_A, {(NvU32)(SWIN >> 32), (NvU32)SWIN});
    m(1, NVC9C0_SET_SHADER_LOCAL_MEMORY_A, {(NvU32)(lm >> 32), (NvU32)lm});
    m(1, NVC9C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A, {(NvU32)(per_tpc >> 32), (NvU32)per_tpc, 0xff});
    m(1, NVC9C0_SEND_PCAS_A, {(NvU32)(qva >> 8)});
    m(1, NVC9C0_SEND_SIGNALING_PCAS2_B, {NVC9C0_SEND_SIGNALING_PCAS2_B_PCAS_ACTION_PREFETCH_SCHEDULE});
    submit("compute");
    int bad = 0;
    for (int i = 0; i < 32; i++) bad += out[i] != 0xA0000000u + i;
    printf("compute: out[0]=0x%x out[31]=0x%x out[32]=0x%x, %d/32 wrong -> %s\n", out[0], out[31], out[32], bad,
           bad || out[32] ? "FAIL" : "OK");
    if (bad || out[32]) return 4;
    if (!strcmp(step, "compute")) return 0;

    // step "inline <sass>": the compute QMD again, delivered as an inline QMD (SET_INLINE_QMD_ADDRESS +
    // LOAD_INLINE_QMD_DATA(0..63)) to an empty slot: is it written to memory, and does it run?
    if (!strcmp(step, "inline")) {
        const NvU64 SLOT = QMDO + 0x200;
        auto* slot = (volatile NvU32*)(cpu + SLOT);
        for (int i = 0; i < 64; i++) slot[i] = 0;
        for (int i = 0; i < 64; i++) out[i] = 0;
        NvU64 sva = gva + SLOT;
        m(1, NVC9C0_SET_INLINE_QMD_ADDRESS_A, {(NvU32)(sva >> 40), (NvU32)(sva >> 8)});
        q.push_back((1u << 29) | (64u << 16) | (1u << 13) | (NVC9C0_LOAD_INLINE_QMD_DATA(0) >> 2));
        for (int i = 0; i < 64; i++) q.push_back(qmd[i]);
        submit("inline");
        int bad = 0, same = 0;
        for (int i = 0; i < 32; i++) bad += out[i] != 0xA0000000u + i;
        for (int i = 0; i < 64; i++) same += slot[i] == qmd[i];
        printf("inline: %d/64 QMD words in memory, kernel %s\n", same, bad ? "did not run" : "ran (32/32 right)");
        return 0;
    }

    // step "barrier <sass> [prog]": an empty-grid QMD (no CTAs) that only releases a semaphore, like the
    // dependency barriers MKW chains; with "prog" it also names a valid program. Does SKED touch the
    // program address of a grid it never runs?
    if (!strcmp(step, "barrier")) {
        const bool prog = argc > 3 && !strcmp(argv[3], "prog");
        NvU64 rel = gva + OUT + 0x200;
        out[0x200 / 4] = 0;
        for (int i = 0; i < 64; i++) qmd[i] = 0;
#define Q(f, v) qs(1 ? NVC6C0_QMDV03_00_##f, 0 ? NVC6C0_QMDV03_00_##f, v)
        Q(QMD_MAJOR_VERSION, 3); Q(SM_GLOBAL_CACHING_ENABLE, 1); Q(API_VISIBLE_CALL_LIMIT, 1);
        Q(CWD_MEMBAR_TYPE, NVC6C0_QMDV03_00_CWD_MEMBAR_TYPE_L1_SYSMEMBAR); Q(REGISTER_COUNT_V, 3);
        Q(CTA_THREAD_DIMENSION0, 1); Q(CTA_THREAD_DIMENSION1, 1); Q(CTA_THREAD_DIMENSION2, 1);
        Q(RELEASE0_ADDRESS_LOWER, (NvU32)rel); Q(RELEASE0_ADDRESS_UPPER, rel >> 32); Q(RELEASE0_ENABLE, 1);
        Q(RELEASE0_MEMBAR_TYPE, 1); Q(RELEASE0_STRUCTURE_SIZE, 1); Q(RELEASE0_PAYLOAD_LOWER, 0xBA77);
        if (prog) { Q(PROGRAM_ADDRESS_LOWER, (NvU32)pva); Q(PROGRAM_ADDRESS_UPPER, pva >> 32); Q(SASS_VERSION, ((smv & 0xf00) >> 4) | (smv & 0xf)); }
#undef Q
        for (int i = 0; i < 64; i++) ((volatile NvU32*)(cpu + QMDO))[i] = qmd[i];
        m(1, NVC9C0_SEND_PCAS_A, {(NvU32)(qva >> 8)});
        m(1, NVC9C0_SEND_SIGNALING_PCAS2_B, {NVC9C0_SEND_SIGNALING_PCAS2_B_PCAS_ACTION_INVALIDATE_COPY_SCHEDULE});
        submit("barrier");
        printf("barrier (%s): release word 0x%x -> %s\n", prog ? "with program" : "program 0", out[0x200 / 4],
               out[0x200 / 4] == 0xBA77 ? "OK" : "FAIL");
        return out[0x200 / 4] == 0xBA77 ? 0 : 6;
    }

    // step "clear": ADA_A clears a 64x64 A8B8G8R8 pitch-linear surface, no shaders. Guard words after it
    // must survive.
    const NvU32 W = 64, H = 64, PITCH = W * 4, GUARD = 64;
    const NvU64 SURF = DATA + 0x40000;
    auto* px = (volatile NvU32*)(cpu + SURF);
    for (NvU32 i = 0; i < W * H + GUARD; i++) px[i] = 0x11111111;
    NvU64 sva = gva + SURF;
    auto f = [](float v) { NvU32 u; memcpy(&u, &v, 4); return u; };
    m(0, NVC997_SET_OBJECT, {ADA_A});
    m(0, NVC997_SET_COLOR_TARGET_A(0), {(NvU32)(sva >> 32), (NvU32)sva, PITCH, H, NVC997_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
        DRF_DEF(C997, _SET_COLOR_TARGET_MEMORY, _LAYOUT, _PITCH), 1, 0, 0});
    m(0, NVC997_SET_CT_SELECT, {1}); // one target, RT0 -> target 0
    m(0, NVC997_SET_ZT_SELECT, {0});
    m(0, NVC997_SET_ANTI_ALIAS, {0});
    m(0, NVC997_SET_SURFACE_CLIP_HORIZONTAL, {W << 16, H << 16});
    m(0, NVC997_SET_CLEAR_SURFACE_CONTROL, {0});
    m(0, NVC997_SET_COLOR_CLEAR_VALUE(0), {f(1.0f), f(0.5f), f(0.25f), f(1.0f)});
    m(0, NVC997_CLEAR_SURFACE, {0x3c}); // RGBA, MRT 0, layer 0
    submit("clear");
    NvU32 want = px[0], wrong = 0, guard = 0;
    for (NvU32 i = 0; i < W * H; i++) wrong += px[i] != want;
    for (NvU32 i = W * H; i < W * H + GUARD; i++) guard += px[i] != 0x11111111;
    bool ok = !wrong && !guard && (want & 0xff) == 0xff && ((want >> 8) & 0xff) >= 0x7f && ((want >> 8) & 0xff) <= 0x80 &&
              ((want >> 16) & 0xff) >= 0x3f && ((want >> 16) & 0xff) <= 0x40 && (want >> 24) == 0xff;
    printf("clear: px[0]=0x%08x, %u/%u pixels differ, %u/%u guard words touched -> %s\n", want, wrong, W * H, guard, GUARD,
           ok ? "OK" : "FAIL");
    return ok ? 0 : 5;
}
