// RM ioctl helpers shared by the passthrough tools (nvrm_probe, pt_replay). Single translation unit each:
// everything is static.
#pragma once
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "nvtypes.h"
#include "nvmisc.h"
#include "nvos.h"
#include "nvstatus.h"
#include "nv_escape.h"
#include "nv-ioctl.h"
#include "nv-ioctl-numbers.h"
#include "nv-unix-nvos-params-wrappers.h"
#include "class/cl0000.h"
#include "class/cl0040.h"
#include "class/cl0070.h"
#include "class/cl0071.h"
#include "class/cl0080.h"
#include "class/cl2080.h"
#include "class/cl2080_notification.h"
#include "class/cl9067.h"
#include "class/cl90f1.h"
#include "class/cla06c.h"
#include "class/clc561.h"
#include "class/clc361.h"
#include "alloc/alloc_channel.h"
#include "class/clc56f.h"
#include "class/clc7b5.h"
#include "class/clc997.h"
#include "class/clc9c0.h"
#include "clc9c0.h"     // mesa: full ADA_COMPUTE_A methods
#include "clc6c0qmd.h"  // mesa: QMD v3 fields
#include "clc997.h"     // mesa: full ADA_A methods
#include "ctrl/ctrl2080/ctrl2080gr.h"
#include "ctrl/ctrl0000/ctrl0000gpu.h"
#include "ctrl/ctrla06c.h"
#include "ctrl/ctrlc36f.h"

#ifndef NV01_ROOT_CLIENT
#define NV01_ROOT_CLIENT 0x41
#endif
#ifndef NV01_MEMORY_SYSTEM
#define NV01_MEMORY_SYSTEM 0x3e
#endif

static int g_ctl = -1, g_dev = -1;
static NvHandle g_root;

static void die(const char* what, NvU32 st) {
    fprintf(stderr, "FAIL %s: status 0x%x (errno %d)\n", what, st, errno);
    exit(1); // atexit frees the root client
}

template <class T> static void nv_ioctl(int fd, int nr, T* p, const char* what) {
    if (ioctl(fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, nr, sizeof(T)), p) < 0) die(what, 0);
}

static NvHandle rm_alloc(NvHandle parent, NvU32 cls, void* params, const char* what) {
    NVOS21_PARAMETERS p{};
    p.hRoot = g_root; p.hObjectParent = parent; p.hClass = cls; p.pAllocParms = params;
    nv_ioctl(g_ctl, NV_ESC_RM_ALLOC, &p, what);
    if (p.status != NV_OK) die(what, p.status);
    return p.hObjectNew;
}

template <class T> static void rm_control(NvHandle obj, NvU32 cmd, T* params, const char* what) {
    NVOS54_PARAMETERS p{};
    p.hClient = g_root; p.hObject = obj; p.cmd = cmd; p.params = params; p.paramsSize = sizeof(T);
    nv_ioctl(g_ctl, NV_ESC_RM_CONTROL, &p, what);
    if (p.status != NV_OK) die(what, p.status);
}

static void free_root() {
    if (!g_root) return;
    NVOS00_PARAMETERS p{};
    p.hRoot = g_root; p.hObjectParent = 0; p.hObjectOld = g_root;
    ioctl(g_ctl, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_FREE, sizeof(p)), &p);
    fprintf(stderr, "freed root client: status 0x%x\n", p.status);
    g_root = 0;
}

static void on_signal(int) { exit(2); } // run atexit cleanup

static int open_dev_fd(NvU32 minor) {
    char path[32];
    snprintf(path, sizeof path, "/dev/nvidia%u", minor);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) die("open /dev/nvidiaN", 0);
    nv_ioctl_register_fd_t r{g_ctl};
    nv_ioctl(fd, NV_ESC_REGISTER_FD, &r, "register fd");
    return fd;
}

static NvHandle g_device, g_subdevice, g_virtmem;
static NvU32 g_minor;

// CPU mapping through BAR1 (vidmem) or sysmem; each mapping gets its own fd, like the driver does.
static void* map_cpu(NvHandle parent, NvHandle mem, NvU64 size, NvU32 flags) {
    nv_ioctl_nvos33_parameters_with_fd m{};
    int fd = open_dev_fd(g_minor);
    m.fd = fd;
    m.params.hClient = g_root; m.params.hDevice = parent; m.params.hMemory = mem;
    m.params.length = size; m.params.flags = flags;
    nv_ioctl(g_ctl, NV_ESC_RM_MAP_MEMORY, &m, "map memory");
    if (m.params.status != NV_OK) die("map memory", m.params.status);
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) die("mmap", 0);
    return p;
}

// GPU mapping of [offset, offset + size) of mem; at va with 4 KiB pages when va != 0, else where RM chooses.
// kind >= 0 overrides the PTE kind (e.g. 0x6 GENERIC_MEMORY instead of the allocation's own).
static NvU64 map_gpu(NvHandle mem, NvU64 size, NvU64 va = 0, NvU64 offset = 0, int kind = -1, bool read_only = false) {
    NVOS46_PARAMETERS p{};
    p.hClient = g_root; p.hDevice = g_device; p.hDma = g_virtmem; p.hMemory = mem; p.offset = offset; p.length = size;
    if (va) {
        p.flags = DRF_DEF(OS46, _FLAGS, _DMA_OFFSET_FIXED, _TRUE) | DRF_DEF(OS46, _FLAGS, _PAGE_SIZE, _4KB);
        p.dmaOffset = va;
    }
    if (read_only) p.flags |= DRF_DEF(OS46, _FLAGS, _ACCESS, _READ_ONLY);
    if (kind >= 0) {
        p.flags |= DRF_DEF(OS46, _FLAGS, _PAGE_KIND_OVERRIDE, _YES);
        p.kindOverride = (NvU32)kind;
    }
    nv_ioctl(g_ctl, NV_ESC_RM_MAP_MEMORY_DMA, &p, "map dma");
    if (p.status != NV_OK) die("map dma", p.status);
    if (va && p.dmaOffset != va) die("map dma: not at the fixed address", 0);
    return p.dmaOffset;
}

static NvHandle alloc_mem(NvU64 size, bool vid) {
    NV_MEMORY_ALLOCATION_PARAMS a{};
    a.owner = g_root; a.size = size; a.alignment = 4096; a.limit = size - 1; a.format = 6;
    if (vid) {
        a.type = NVOS32_TYPE_IMAGE;
        a.attr = DRF_DEF(OS32, _ATTR, _PHYSICALITY, _CONTIGUOUS) | DRF_DEF(OS32, _ATTR, _LOCATION, _VIDMEM);
        a.attr2 = DRF_DEF(OS32, _ATTR2, _GPU_CACHEABLE, _YES) | DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC);
        a.flags = NVOS32_ALLOC_FLAGS_MAP_NOT_REQUIRED | NVOS32_ALLOC_FLAGS_ALIGNMENT_FORCE |
                  NVOS32_ALLOC_FLAGS_IGNORE_BANK_PLACEMENT | NVOS32_ALLOC_FLAGS_PERSISTENT_VIDMEM;
        return rm_alloc(g_device, NV1_MEMORY_USER, &a, "alloc vidmem");
    }
    a.type = NVOS32_TYPE_NOTIFIER;
    a.attr = DRF_DEF(OS32, _ATTR, _PHYSICALITY, _NONCONTIGUOUS) | DRF_DEF(OS32, _ATTR, _LOCATION, _PCI);
    a.attr2 = DRF_DEF(OS32, _ATTR2, _GPU_CACHEABLE, _NO) | DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC);
    a.flags = NVOS32_ALLOC_FLAGS_MAP_NOT_REQUIRED | NVOS32_ALLOC_FLAGS_ALIGNMENT_FORCE |
              NVOS32_ALLOC_FLAGS_IGNORE_BANK_PLACEMENT;
    return rm_alloc(g_device, NV01_MEMORY_SYSTEM, &a, "alloc sysmem");
}

// Pinned host memory the GPU can map, CPU-cached and snooped: our own anonymous mapping wrapped as
// NV01_MEMORY_SYSTEM_OS_DESCRIPTOR (what tinygrad does). RM's CPU maps of its own sysmem are refused here.
[[maybe_unused]] static NvHandle alloc_host(NvU64 size, NvU8** cpu) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) die("mmap host memory", 0);
    nv_ioctl_nvos02_parameters_with_fd m{};
    m.fd = -1;
    static NvHandle next = 0x70000000; // this escape takes the new handle from the client
    m.params.hRoot = g_root; m.params.hObjectParent = g_device; m.params.hObjectNew = next++;
    m.params.hClass = NV01_MEMORY_SYSTEM_OS_DESCRIPTOR;
    m.params.flags = DRF_DEF(OS02, _FLAGS, _PHYSICALITY, _NONCONTIGUOUS) | DRF_DEF(OS02, _FLAGS, _COHERENCY, _CACHED) |
                     DRF_DEF(OS02, _FLAGS, _MAPPING, _NO_MAP);
    m.params.pMemory = (NvP64)p; m.params.limit = size - 1;
    nv_ioctl(g_dev, NV_ESC_RM_ALLOC_MEMORY, &m, "alloc os descriptor"); // on the device fd
    if (m.params.status != NV_OK) die("alloc os descriptor", m.params.status);
    *cpu = (NvU8*)p;
    return m.params.hObjectNew;
}

[[maybe_unused]] static NvU64 rup(NvU64 v, NvU64 a) { return (v + a - 1) / a * a; }
[[maybe_unused]] static NvU32 hdr(NvU32 subc, NvU32 mthd, NvU32 n) { return (1u << 29) | (n << 16) | (subc << 13) | (mthd >> 2); }

static NvHandle g_vas;
static nv_ioctl_card_info_t g_card;

// Root client -> device -> subdevice -> RM-owned VA space with a 1 TiB virtual window at 0 (semaphore and
// pushbuffer addresses are 40-bit). Frees everything at exit and on SIGINT/TERM/HUP.
static void rm_open() {
    atexit(free_root);
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGHUP, on_signal);
    g_ctl = open("/dev/nvidiactl", O_RDWR | O_CLOEXEC);
    if (g_ctl < 0) die("open nvidiactl", 0);
    nv_ioctl_card_info_t cards[16]{};
    nv_ioctl(g_ctl, NV_ESC_CARD_INFO, &cards, "card info");
    if (!cards[0].valid) die("no GPU", 0);
    g_card = cards[0];
    g_minor = cards[0].minor_number;

    g_root = 0;
    { NVOS21_PARAMETERS p{}; p.hClass = NV01_ROOT_CLIENT;
      nv_ioctl(g_ctl, NV_ESC_RM_ALLOC, &p, "root"); if (p.status) die("root", p.status); g_root = p.hObjectNew; }
    g_dev = open_dev_fd(g_minor);

    NV0000_CTRL_GPU_GET_ID_INFO_V2_PARAMS id{}; id.gpuId = cards[0].gpu_id;
    rm_control(g_root, NV0000_CTRL_CMD_GPU_GET_ID_INFO_V2, &id, "gpu id info");

    NV0080_ALLOC_PARAMETERS dp{}; dp.deviceId = id.deviceInstance; dp.hClientShare = g_root;
    dp.vaMode = NV_DEVICE_ALLOCATION_VAMODE_OPTIONAL_MULTIPLE_VASPACES;
    g_device = rm_alloc(g_root, NV01_DEVICE_0, &dp, "device");
    NV2080_ALLOC_PARAMETERS sp{};
    g_subdevice = rm_alloc(g_device, NV20_SUBDEVICE_0, &sp, "subdevice");

    NV_VASPACE_ALLOCATION_PARAMETERS vp{}; // RM-owned VA space, no UVM
    g_vas = rm_alloc(g_device, FERMI_VASPACE_A, &vp, "vaspace");
    NV_MEMORY_VIRTUAL_ALLOCATION_PARAMS mv{}; mv.limit = (1ull << 40) - 1; mv.hVASpace = g_vas;
    g_virtmem = rm_alloc(g_device, NV01_MEMORY_VIRTUAL, &mv, "virtmem");
}
