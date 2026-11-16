// Phase 1: replay one captured guest frame on the host GPU (capture: Eglantine EDEN_NX2_PT_CAPTURE,
// rewritten for the host by `ptcap.py build`). Guest memory becomes pinned sysmem mapped at the guest's own
// GPU VAs; each guest channel gets its own host TSG + channel; entries run one at a time in the recorded
// order, each followed by a host semaphore release we wait for.
// usage: pt_replay <capture>/replay [entries to run] [va:size:file ...]   (readbacks after the run or a timeout)
#include "nvrm.h"

#include <map>
#include <string>
#include <utility>

struct Block { NvHandle h; NvU8* cpu; NvU64 size; };
struct Piece { NvU64 va, size; int block; NvU64 off; };
struct Chan {
    volatile NvU8* cpu;
    NvU64 gva;
    NvU32 token, gpput = 0, pbn = 0, fence = 0;
    std::vector<NvU32> classes;
};

static const NvU64 CH_VA = 0xF800000000ull, CH_SZ = 4 << 20, RING = 0, USERD = 0x2000, SEM = 0x3000, PB = 0x10000;
static const NvU32 ENTRIES = 0x400;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <replay dir> [entries] [va:size:file ...]\n", argv[0]); return 1; }
    std::string dir = argv[1], cap = dir + "/..";
    long limit = argc > 2 ? strtol(argv[2], nullptr, 0) : -1;
    rm_open();
    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // PT_KIND=<hex>: PTE kind for guest memory (default: the allocation's, pitch).
    const int guest_kind = getenv("PT_KIND") ? (int)strtol(getenv("PT_KIND"), nullptr, 16) : -1;
    std::map<int, Block> blocks;
    std::vector<Piece> pieces;
    std::map<int, Chan> chans;
    FILE* plan = fopen((dir + "/plan.txt").c_str(), "r");
    if (!plan) die("open plan.txt", 0);
    char kind[16];
    NvU64 filled = 0;
    while (fscanf(plan, "%15s", kind) == 1) {
        std::string k = kind;
        if (k == "block") {
            int id; unsigned long long size;
            if (fscanf(plan, "%d %llx", &id, &size) != 2) die("plan: block", 0);
            Block b{0, nullptr, size};
            b.h = alloc_host(size, &b.cpu);
            blocks[id] = b;
        } else if (k == "fill") {
            int id; unsigned long long off, foff, n; char file[64];
            if (fscanf(plan, "%d %llx %63s %llx %llx", &id, &off, file, &foff, &n) != 5) die("plan: fill", 0);
            Block& b = blocks.at(id);
            if (off + n > b.size) die("plan: fill past its block", 0);
            int fd = open((cap + "/" + file).c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) die("open dev file", 0);
            for (NvU64 done = 0; done < n;) {
                ssize_t r = pread(fd, b.cpu + off + done, n - done, foff + done);
                if (r <= 0) die("read dev file", 0);
                done += r;
            }
            close(fd);
            filled += n;
        } else if (k == "map") {
            Piece p; unsigned long long va, size, off;
            if (fscanf(plan, "%llx %llx %d %llx", &va, &size, &p.block, &off) != 4) die("plan: map", 0);
            p.va = va; p.size = size; p.off = off;
            Block& b = blocks.at(p.block);
            if (p.off + p.size > b.size || p.va + p.size > (1ull << 40)) die("plan: map out of range", 0);
            map_gpu(b.h, p.size, p.va, p.off, guest_kind);
            pieces.push_back(p);
        } else if (k == "channel") {
            int idx; unsigned c[8];
            if (fscanf(plan, "%d %x %x %x %x %x %x %x %x", &idx, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7]) != 9)
                die("plan: channel", 0);
            chans[idx].classes.assign(c, c + 8);
        } else {
            die("plan: unknown line", 0);
        }
    }
    fclose(plan);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("memory: %zu blocks, %zu mappings, %llu MiB filled, %.1f s\n", blocks.size(), pieces.size(),
           (unsigned long long)(filled >> 20), (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9);

    // One TSG + channel per guest channel, all on the one VA space, so each keeps its own GR context.
    NvHandle usermode = rm_alloc(g_subdevice, AMPERE_USERMODE_A, nullptr, "usermode");
    auto* mmio = (volatile NvU32*)map_cpu(g_device, usermode, 0x10000, 0);
    for (auto& [idx, c] : chans) {
        NvHandle mem = alloc_mem(CH_SZ, true);
        c.cpu = (volatile NvU8*)map_cpu(g_device, mem, CH_SZ, DRF_DEF(OS33, _FLAGS, _CACHING_TYPE, _WRITECOMBINED));
        c.gva = map_gpu(mem, CH_SZ, CH_VA + idx * CH_SZ);
        NvHandle notifier = alloc_mem(64 << 10, false);
        NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS tp{}; tp.engineType = NV2080_ENGINE_TYPE_GRAPHICS; tp.hVASpace = g_vas;
        NvHandle tsg = rm_alloc(g_device, KEPLER_CHANNEL_GROUP_A, &tp, "tsg");
        NV_CTXSHARE_ALLOCATION_PARAMETERS cp{}; cp.hVASpace = g_vas; cp.flags = NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_SYNC;
        NvHandle ctxshare = rm_alloc(tsg, FERMI_CONTEXT_SHARE_A, &cp, "ctxshare");
        NV_CHANNEL_ALLOC_PARAMS ch{};
        ch.hObjectError = notifier; ch.hObjectBuffer = mem; ch.gpFifoOffset = c.gva + RING; ch.gpFifoEntries = ENTRIES;
        ch.hContextShare = ctxshare; ch.hUserdMemory[0] = mem; ch.userdOffset[0] = USERD;
        NvHandle chan = rm_alloc(tsg, AMPERE_CHANNEL_GPFIFO_A, &ch, "channel");
        for (NvU32 cls : c.classes)
            if (cls) rm_alloc(chan, cls, nullptr, "engine object");
        NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS sch{}; sch.bEnable = NV_TRUE;
        rm_control(tsg, NVA06C_CTRL_CMD_GPFIFO_SCHEDULE, &sch, "schedule");
        NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS tok{}; tok.workSubmitToken = ~0u;
        rm_control(chan, NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN, &tok, "work submit token");
        c.token = tok.workSubmitToken;
        *(volatile NvU32*)(c.cpu + SEM) = 0;
        printf("channel %d: gva 0x%llx token 0x%x\n", idx, (unsigned long long)c.gva, c.token);
    }

    // Readbacks (after the run, or after a timeout: host memory outlives a dead channel).
    auto readbacks = [&] {
        for (int a = 3; a < argc; a++) {
            unsigned long long va, size; char file[512];
            if (sscanf(argv[a], "%llx:%llx:%511s", &va, &size, file) != 3) die("readback: va:size:file", 0);
            FILE* f = fopen(file, "wb");
            if (!f) die("open readback file", 0);
            NvU64 done = 0;
            std::vector<NvU8> buf;
            while (done < size) {
                NvU64 at = va + done;
                const Piece* p = nullptr;
                for (const Piece& q : pieces)
                    if (q.va <= at && at < q.va + q.size) { p = &q; break; }
                if (!p) die("readback: address not mapped", 0);
                NvU64 n = std::min<NvU64>(size - done, p->va + p->size - at);
                buf.resize(n);
                memcpy(buf.data(), blocks.at(p->block).cpu + p->off + (at - p->va), n);
                fwrite(buf.data(), 1, n, f);
                done += n;
            }
            fclose(f);
            printf("readback 0x%llx +0x%llx -> %s\n", va, size, file);
        }
    };

    FILE* push = fopen((dir + "/push.bin").c_str(), "rb");
    if (!push) die("open push.bin", 0);
    std::vector<NvU32> words;
    long ran = 0;
    double busy = 0, worst = 0;
    long worst_k = 0;
    NvU32 hdr3[3];
    while ((limit < 0 || ran < limit) && fread(hdr3, 4, 3, push) == 3) {
        int idx = (int)hdr3[0], k = (int)hdr3[1];
        words.resize(hdr3[2] + 6);
        if (fread(words.data(), 4, hdr3[2], push) != hdr3[2]) die("short push.bin", 0);
        Chan& c = chans.at(idx);
        NvU64 sa = c.gva + SEM;
        NvU32* w = words.data() + hdr3[2];
        w[0] = hdr(0, NVC56F_SEM_ADDR_LO, 5); w[1] = (NvU32)sa; w[2] = (NvU32)(sa >> 32); w[3] = ++c.fence; w[4] = 0;
        w[5] = DRF_DEF(C56F, _SEM_EXECUTE, _OPERATION, _RELEASE) | DRF_DEF(C56F, _SEM_EXECUTE, _RELEASE_WFI, _EN) |
               DRF_DEF(C56F, _SEM_EXECUTE, _PAYLOAD_SIZE, _32BIT);
        const NvU32 cap_words = (CH_SZ - PB) / 4;
        if (words.size() > cap_words) die("entry larger than the pushbuffer", 0);
        if (c.pbn + words.size() > cap_words) c.pbn = 0; // the previous entry has completed: reuse from the start
        auto* pbw = (volatile NvU32*)(c.cpu + PB);
        for (size_t i = 0; i < words.size(); i++) pbw[c.pbn + i] = words[i];
        NvU64 pa = c.gva + PB + c.pbn * 4ull;
        c.pbn += words.size();
        auto* userd = (volatile AmpereAControlGPFifo*)(c.cpu + USERD);
        ((volatile NvU64*)(c.cpu + RING))[c.gpput % ENTRIES] = pa | ((NvU64)words.size() << 42);
        __sync_synchronize();
        userd->GPPut = ++c.gpput % ENTRIES;
        __sync_synchronize();
        mmio[NVC361_NOTIFY_CHANNEL_PENDING / 4] = c.token;
        timespec s0, s;
        clock_gettime(CLOCK_MONOTONIC, &s0);
        auto* sem = (volatile NvU32*)(c.cpu + SEM);
        for (;;) {
            if (*sem == c.fence) break;
            clock_gettime(CLOCK_MONOTONIC, &s);
            if (s.tv_sec - s0.tv_sec > 5) {
                printf("entry %ld (capture entry %d, channel %d, %u words): TIMEOUT, sem %u want %u, GPGet %u GPPut %u\n",
                       ran, k, idx, hdr3[2], *sem, c.fence, userd->GPGet, userd->GPPut);
                readbacks();
                return 3;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &s);
        double us = (s.tv_sec - s0.tv_sec) * 1e6 + (s.tv_nsec - s0.tv_nsec) / 1e3;
        busy += us;
        if (us > worst) { worst = us; worst_k = k; }
        ran++;
    }
    fclose(push);
    printf("ran %ld entries, %.1f ms GPU wait in total, slowest %.1f ms (capture entry %ld)\n", ran, busy / 1e3, worst / 1e3, worst_k);

    readbacks();
    return 0;
}
