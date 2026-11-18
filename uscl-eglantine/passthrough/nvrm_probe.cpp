// Phase 0 probe for the Eglantine GPU passthrough backend: reach the host NVIDIA GPU through the
// RM ioctls the userspace driver itself uses, on our own channel, and run tiny submissions.
// Steps (argv[1]): "alloc" = objects only, "sem" = + one host semaphore release,
// "guestvm [GiB] [granule MiB]" = sem, then the Phase 2 guest VA checks (sparse range, OS descriptor import, kinds).
// "compute <sass>" = + one compute launch writing a buffer, "clear <sass>" = + an ADA_A surface clear,
// "mthd <offset> <value>" = sem, then one ADA_A method, "barrier <sass> [prog]" = compute, then an empty-grid
// QMD that only releases a semaphore, "inline <sass> [sys]" = compute, then the same QMD as an inline QMD (slot in host memory with sys).
// Safety: the root client is freed on every exit path (RM frees all children with it).
#include "nvrm.h"
#include <sys/syscall.h>
#include "class/cl50a0.h" // NV50_MEMORY_VIRTUAL (guestvm: the sparse guest VA range)

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
    bool timeout_ok = false; // the "macro" step reads its slot even when the work it launched fails
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
                if (timeout_ok) return;
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

    // step "guestvm [GiB] [granule MiB, 0 = one descriptor]" (Phase 2 step 2, gate 4): a guest VA range as one RM
    // sparse virtual allocation (unmapped reads return 0), guest memory as a memfd shared mapping (as Eglantine's
    // HostMemory) imported as OS descriptors, mapped inside the range at fixed VAs with MKW's PTE kinds. Checks:
    // the import (pinning) time, every kind's map status, a semaphore release through the import seen by the CPU,
    // a copy from an unmapped sparse VA that must read zeros.
    if (!strcmp(step, "guestvm")) {
        const NvU64 gib = argc > 2 ? strtoull(argv[2], nullptr, 0) : 4, gran_mib = argc > 3 ? strtoull(argv[3], nullptr, 0) : 64;
        const NvU64 GBASE = 1ull << 39, GSIZE = 1ull << 38, HSIZE = gib << 30; // our channel memory sits below 1 << 39
        NV_MEMORY_ALLOCATION_PARAMS va{};
        va.owner = g_root; va.type = NVOS32_TYPE_IMAGE; va.size = GSIZE; va.offset = GBASE; va.hVASpace = g_vas;
        va.flags = NVOS32_ALLOC_FLAGS_VIRTUAL | NVOS32_ALLOC_FLAGS_SPARSE | NVOS32_ALLOC_FLAGS_FIXED_ADDRESS_ALLOCATE;
        va.attr = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _4KB);
        NvHandle gvm = rm_alloc(g_device, NV50_MEMORY_VIRTUAL, &va, "sparse guest VA");
        printf("sparse guest VA: [0x%llx, +0x%llx) at 0x%llx\n", (unsigned long long)GBASE, (unsigned long long)GSIZE,
               (unsigned long long)va.offset);
        const int mfd = (int)syscall(SYS_memfd_create, "guestvm", 0);
        if (mfd < 0 || ftruncate(mfd, (off_t)HSIZE)) die("memfd", 0);
        auto* host = (NvU8*)mmap(nullptr, HSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
        if (host == MAP_FAILED) die("mmap memfd", 0);
        const NvU64 gran = gran_mib ? gran_mib << 20 : HSIZE;
        std::vector<NvHandle> descs;
        timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (NvU64 o = 0; o < HSIZE; o += gran) {
            nv_ioctl_nvos02_parameters_with_fd d{};
            d.fd = -1;
            static NvHandle next = 0x71000000;
            d.params.hRoot = g_root; d.params.hObjectParent = g_device; d.params.hObjectNew = next++;
            d.params.hClass = NV01_MEMORY_SYSTEM_OS_DESCRIPTOR;
            d.params.flags = DRF_DEF(OS02, _FLAGS, _PHYSICALITY, _NONCONTIGUOUS) | DRF_DEF(OS02, _FLAGS, _COHERENCY, _CACHED) |
                             DRF_DEF(OS02, _FLAGS, _MAPPING, _NO_MAP);
            d.params.pMemory = (NvP64)(host + o); d.params.limit = gran - 1;
            nv_ioctl(g_dev, NV_ESC_RM_ALLOC_MEMORY, &d, "os descriptor");
            if (d.params.status != NV_OK) { printf("os descriptor at +0x%llx: status 0x%x\n", (unsigned long long)o, d.params.status); die("os descriptor", d.params.status); }
            descs.push_back(d.params.hObjectNew);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        { FILE* st = fopen("/proc/self/status", "r"); char line[256];
          while (st && fgets(line, sizeof line, st)) if (!strncmp(line, "RssShmem", 8) || !strncmp(line, "VmPin", 5) || !strncmp(line, "VmLck", 5)) printf("  %s", line);
          if (st) fclose(st); }
        printf("import: %llu GiB as %zu descriptor(s) of %llu MiB in %ld ms\n", (unsigned long long)gib, descs.size(),
               (unsigned long long)(gran >> 20), ms);
        // Map: the first 1 MiB of descriptor 0 at GBASE + i * 16 MiB for each kind (MKW: 6/6, 8/6, b/1, c/3, d/4, 9/6
        // as guest kind/fallback; 0 = pitch, 0xfe = generic 16Bx2). Offsets inside a sparse range are absolute VAs.
        const int kinds[] = {0x00, 0x06, 0x08, 0x09, 0x0b, 0x01, 0x0c, 0x03, 0x0d, 0x04, 0xfe};
        for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
            NVOS46_PARAMETERS p{};
            p.hClient = g_root; p.hDevice = g_device; p.hDma = gvm; p.hMemory = descs[0]; p.offset = 0; p.length = 1 << 20;
            p.flags = DRF_DEF(OS46, _FLAGS, _DMA_OFFSET_FIXED, _TRUE) | DRF_DEF(OS46, _FLAGS, _PAGE_SIZE, _4KB) |
                      DRF_DEF(OS46, _FLAGS, _PAGE_KIND_OVERRIDE, _YES);
            p.kindOverride = (NvU32)kinds[i];
            p.dmaOffset = GBASE + i * (16ull << 20);
            nv_ioctl(g_ctl, NV_ESC_RM_MAP_MEMORY_DMA, &p, "map dma");
            printf("  kind 0x%02x: status 0x%x at 0x%llx\n", kinds[i], p.status, (unsigned long long)p.dmaOffset);
        }
        // GPU checks through the pitch mapping (kind 0 at GBASE): a semaphore release to GBASE + 0x100, and a copy of
        // 4 KiB from an unmapped sparse VA (GBASE + 0x100000000) to GBASE + 0x1000 (pre-filled with 0xAB).
        *(volatile NvU32*)(host + 0x100) = 0;
        memset(host + 0x1000, 0xAB, 0x1000);
        __sync_synchronize();
        m(0, NVC56F_SEM_ADDR_LO, {(NvU32)(GBASE + 0x100), (NvU32)((GBASE + 0x100) >> 32), 0x600D5EED, 0,
            DRF_DEF(C56F, _SEM_EXECUTE, _OPERATION, _RELEASE) | DRF_DEF(C56F, _SEM_EXECUTE, _PAYLOAD_SIZE, _32BIT)});
        m(4, 0, {AMPERE_DMA_COPY_B});
        const NvU64 s = GBASE + 0x100000000ull, d = GBASE + 0x1000;
        m(4, NVC7B5_OFFSET_IN_UPPER, {(NvU32)(s >> 32), (NvU32)s, (NvU32)(d >> 32), (NvU32)d});
        m(4, NVC7B5_LINE_LENGTH_IN, {0x1000, 1});
        m(4, NVC7B5_LAUNCH_DMA, {DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
            DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE, _TRUE) | DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT, _PITCH) |
            DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT, _PITCH)});
        submit("guestvm");
        int zeros = 0;
        for (int i = 0; i < 0x1000; i++) zeros += host[0x1000 + i] == 0;
        printf("guestvm: semaphore through the import 0x%x (want 0x600d5eed); sparse read: %d/4096 bytes zero -> %s\n",
               *(volatile NvU32*)(host + 0x100), zeros,
               *(volatile NvU32*)(host + 0x100) == 0x600D5EED && zeros == 0x1000 ? "OK" : "FAIL");
        return 0; // free_root (atexit) frees the descriptors, mappings and the range
    }

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

    // step "macro <sass> <macro file> <qmd file>": the guest's MME program and start addresses (macro file:
    // u32 code words, u32 start entries, u32 params, code, starts, params; ptcap-extracted), then one call
    // of compute macro 57 with the captured parameters, its QMD address moved to a probe slot and the QMD's
    // word 4 bit 12 set (no launch). Prints how the written QMD lines up with Eglantine's (qmd file).
    if (!strcmp(step, "macro")) {
        if (argc < 5) { fprintf(stderr, "macro needs <sass> <macro file> <qmd file>\n"); return 1; }
        std::vector<NvU32> f, eg(64);
        { FILE* mf = fopen(argv[3], "rb"); if (!mf) die("open macro file", 0); NvU32 v; while (fread(&v, 4, 1, mf) == 1) f.push_back(v); fclose(mf); }
        { FILE* qf = fopen(argv[4], "rb"); if (!qf || fread(eg.data(), 4, 64, qf) != 64) die("read qmd file", 0); fclose(qf); }
        NvU32 ncode = f[0], nstart = f[1], nparam = f[2];
        const NvU32* code = &f[3]; const NvU32* starts = code + ncode; std::vector<NvU32> params(starts + nstart, starts + nstart + nparam);
        const NvU64 SLOT = QMDO + 0x400;
        auto* slot = (volatile NvU32*)(cpu + SLOT);
        for (int i = 0; i < 64; i++) slot[i] = 0;
        NvU64 sva = gva + SLOT;
        params[0] = (NvU32)(sva >> 8);
        // Guest addresses the macro's work touches (its QMD's release, cbufs): zeroed scratch at the same VAs.
        for (NvU64 va : {0x2104e00000ull, 0x2109800000ull}) {
            NvHandle sm = alloc_mem(2 << 20, true);
            map_gpu(sm, 2 << 20, va);
        }
        for (auto& p : params) if (p == 0x5f) { p |= 0x1000; break; } // QMD word 4: write, don't launch
        eg[4] |= 0x1000;
        m(1, NVC9C0_SET_OBJECT, {ADA_COMPUTE_A});
        m(1, NVC9C0_LOAD_MME_INSTRUCTION_RAM_POINTER, {0});
        for (NvU32 i = 0; i < ncode; i += 0x1000) {
            NvU32 n = std::min<NvU32>(0x1000, ncode - i);
            q.push_back((3u << 29) | (n << 16) | (1u << 13) | (NVC9C0_LOAD_MME_INSTRUCTION_RAM >> 2));
            q.insert(q.end(), code + i, code + i + n);
        }
        m(1, NVC9C0_LOAD_MME_START_ADDRESS_RAM_POINTER, {0});
        q.push_back((3u << 29) | (nstart << 16) | (1u << 13) | (NVC9C0_LOAD_MME_START_ADDRESS_RAM >> 2));
        q.insert(q.end(), starts, starts + nstart);
        m(1, NVC9C0_CALL_MME_MACRO(57), {params[0]});
        q.push_back((3u << 29) | ((nparam - 1) << 16) | (1u << 13) | (NVC9C0_CALL_MME_DATA(57) >> 2));
        q.insert(q.end(), params.begin() + 1, params.end());
        timeout_ok = true;
        submit("macro");
        for (int k = -4; k <= 4; k++) {
            int same = 0;
            for (int i = 0; i < 64; i++) if (i + k >= 0 && i + k < 64) same += slot[i + k] == eg[i];
            if (same >= 56) printf("macro: slot matches Eglantine's QMD shifted by %d words (%d/64 equal)\n", k, same);
        }
        printf("macro: slot w0..7: %08x %08x %08x %08x %08x %08x %08x %08x\n", slot[0], slot[1], slot[2], slot[3], slot[4], slot[5], slot[6], slot[7]);
        return 0;
    }

    // step "inline <sass>": the compute QMD again, delivered as an inline QMD (SET_INLINE_QMD_ADDRESS +
    // LOAD_INLINE_QMD_DATA(0..63)) to an empty slot: is it written to memory, and does it run?
    if (!strcmp(step, "inline")) {
        const NvU64 SLOT = QMDO + 0x400; // past cbuf0 (QMDO + 0x100, 0x200 bytes)
        volatile NvU32* slot = (volatile NvU32*)(cpu + SLOT);
        NvU64 sva = gva + SLOT;
        if (argc > 3 && !strcmp(argv[3], "sys")) { // the slot in pinned host memory, as pt_replay maps guest memory
            NvU8* host;
            NvHandle h = alloc_host(1 << 16, &host);
            sva = map_gpu(h, 1 << 16);
            slot = (volatile NvU32*)host;
        }
        for (int i = 0; i < 64; i++) slot[i] = 0;
        for (int i = 0; i < 64; i++) out[i] = 0;
        // order (argv[4]): "ab" = A, B, data (default); "b" = B only, data (what NVN's macro emits);
        // "bb" = a lone B first (as a state restore would), then B, data.
        const char* order = argc > 4 ? argv[4] : "ab";
        if (!strcmp(order, "ab")) m(1, NVC9C0_SET_INLINE_QMD_ADDRESS_A, {(NvU32)(sva >> 40), (NvU32)(sva >> 8)});
        else if (!strcmp(order, "b1")) { // B and the 64 words in one incrementing run, as NVN's macro emits them
            q.push_back((1u << 29) | (65u << 16) | (1u << 13) | (NVC9C0_SET_INLINE_QMD_ADDRESS_B >> 2));
            q.push_back((NvU32)(sva >> 8));
            for (int i = 0; i < 64; i++) q.push_back(qmd[i]);
        } else {
            if (!strcmp(order, "bb")) m(1, NVC9C0_SET_INLINE_QMD_ADDRESS_B, {(NvU32)(sva >> 8)});
            m(1, NVC9C0_SET_INLINE_QMD_ADDRESS_B, {(NvU32)(sva >> 8)});
        }
        if (strcmp(order, "b1")) {
            q.push_back((1u << 29) | (64u << 16) | (1u << 13) | (NVC9C0_LOAD_INLINE_QMD_DATA(0) >> 2));
            for (int i = 0; i < 64; i++) q.push_back(qmd[i]);
        }
        timeout_ok = true;
        submit("inline");
        int bad = 0, same = 0;
        for (int i = 0; i < 32; i++) bad += out[i] != 0xA0000000u + i;
        for (int i = 0; i < 64; i++) same += slot[i] == qmd[i];
        int shift = -1;
        for (int k = 0; k < 4 && shift < 0; k++) {
            bool ok = true;
            for (int i = 0; i + k < 64; i++) ok = ok && slot[i + k] == qmd[i];
            if (ok) shift = k;
        }
        printf("inline (%s): %d/64 QMD words in memory (shift %d), kernel %s\n", order, same, shift,
               bad ? "did not run" : "ran (32/32 right)");
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
