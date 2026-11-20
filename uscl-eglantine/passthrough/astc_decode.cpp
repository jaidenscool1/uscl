// ptcap.py build PT_ASTC=decode: Eglantine's own ASTC decoder (video_core/textures/astc.cpp, built in
// unchanged) behind a C entry point for ctypes. Linear ASTC blocks in, linear RGBA8 out.
#include <cstddef>
#include <cstdint>
#include <span>
#include "video_core/textures/astc.cpp"

extern "C" void pt_astc_decode(const uint8_t* in, size_t in_size, uint32_t w, uint32_t h, uint32_t d,
                               uint32_t bw, uint32_t bh, uint8_t* out) {
    Tegra::Texture::ASTC::Decompress({in, in_size}, w, h, d, bw, bh, {out, size_t{w} * h * d * 4});
}
