// Runs HelperLoads::Patch on program images: harness <dir>; for each <va>.img writes <va>.cpp.out
// (u32 loads, u32 skipped-len, skipped, u32 program-len, program, u32 tramp-len, tramp).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "video_core/nx2_helper_loads.h"
int main(int, char** argv) {
    namespace fs = std::filesystem;
    for (auto& e : fs::directory_iterator(argv[1])) {
        if (e.path().extension() != ".img") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<u8> img((std::istreambuf_iterator<char>(f)), {});
        const u64 va = std::stoull(e.path().stem().string(), nullptr, 16);
        auto r = Tegra::Nx2Passthrough::HelperLoads::Patch(va, img, 0x9000000000ULL);
        std::ofstream o(e.path().string().substr(0, e.path().string().size() - 4) + ".cpp.out", std::ios::binary);
        auto w32 = [&](u32 v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        w32(r.loads); w32(static_cast<u32>(r.skipped.size())); o << r.skipped;
        w32(static_cast<u32>(r.program.size())); o.write(reinterpret_cast<const char*>(r.program.data()), r.program.size());
        w32(static_cast<u32>(r.trampolines.size())); o.write(reinterpret_cast<const char*>(r.trampolines.data()), r.trampolines.size());
    }
}
