#include "canvas.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include "../third_party/stb_image.h"

namespace uscl {
namespace {
uint32_t blend(uint32_t bg, uint32_t fg, int a) {
    uint32_t r = 0xff000000;
    for (int n = 0; n < 3; ++n) {
        int shift = n * 8;
        r |= ((((fg >> shift) & 255) * a + ((bg >> shift) & 255) * (255 - a)) / 255) << shift;
    }
    return r;
}
uint32_t codepoint(const std::string& s, size_t& i) {
    unsigned char c = s[i++];
    if (c < 128) return c;
    int n = c >= 0xf0 && c <= 0xf4 ? 3 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : 0;
    if (!n || s.size() - i < static_cast<size_t>(n)) return '?';
    uint32_t cp = c & ((1u << (6 - n)) - 1);
    for (int j = 0; j < n; ++j) {
        unsigned char b = s[i];
        if ((b & 0xc0) != 0x80) return '?';
        ++i; cp = cp << 6 | (b & 63);
    }
    return cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
        (n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ? '?' : cp;
}
}
Canvas::Canvas(const std::string& path) : assets(path), pixels(width * height) {
    if (FT_Init_FreeType(&ft) == 0) FT_New_Face(ft, (assets + "/font.ttf").c_str(), 0, &face);
}
Canvas::~Canvas() { if (face) FT_Done_Face(face); if (ft) FT_Done_FreeType(ft); }
void Canvas::clear() {
    for (int y = 0; y < height; ++y) {
        int shade = 34 + (height - y) * 45 / height;
        std::fill(pixels.begin() + y * width, pixels.begin() + (y + 1) * width,
                  0xff000000u | uint32_t(shade * 1.45) << 16 | uint32_t(shade * .8) << 8 | uint32_t(shade * 1.9));
    }
    for (int x = 0; x < width; ++x) {
        int y = 610 + int(95 * std::sin(x / 400.0));
        for (int k = 0; k < 5; ++k) pixels[(y + k) * width + x] = blend(pixels[(y + k) * width + x], 0xe5caff, 35 - k * 5);
    }
}
void Canvas::rect(int x, int y, int w, int h, uint32_t c) {
    for (int row = std::max(0, y); row < std::min(height, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(width, x + w); ++col)
            pixels[row * width + col] = 0xff000000 | c;
}
void Canvas::text(const std::string& s, int x, int baseline, int size, uint32_t color, int maxWidth) {
    if (!face) return;
    FT_Set_Pixel_Sizes(face, 0, size);
    int cursor = x;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = codepoint(s, i);
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        auto slot = face->glyph;
        int advance = slot->advance.x >> 6;
        if (cursor + advance > x + maxWidth) break;
        const auto& bitmap = slot->bitmap;
        for (int row = 0; row < int(bitmap.rows); ++row) {
            const auto* line = bitmap.buffer + (bitmap.pitch >= 0 ? row : int(bitmap.rows) - 1 - row) * std::abs(bitmap.pitch);
            for (int col = 0; col < int(bitmap.width); ++col) {
                int px = cursor + slot->bitmap_left + col, py = baseline - slot->bitmap_top + row;
                if (px >= 0 && px < width && py >= 0 && py < height)
                    pixels[py * width + px] = blend(pixels[py * width + px], color, line[col]);
            }
        }
        cursor += advance;
    }
}
void Canvas::image(const std::string& path, int x, int y, int w, int h, int alpha) {
    // Cache only the small set of UI images and the focused title's icon.
    auto it = images.find(path);
    if (it == images.end()) {
        if (images.size() > 24) images.clear();
        auto bytes = readFile(path, 8 * 1024 * 1024);
        int iw = 0, ih = 0, channels = 0;
        uint8_t* data = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), int(bytes.size()), &iw, &ih, &channels, 4);
        std::vector<uint8_t> scaled;
        if (data && iw > 0 && ih > 0) {
            // Store at a bounded 320x320 resolution to keep library browsing memory stable.
            scaled.resize(320 * 320 * 4);
            for (int row = 0; row < 320; ++row)
                for (int col = 0; col < 320; ++col)
                    std::memcpy(&scaled[(row * 320 + col) * 4], data + ((row * ih / 320) * iw + col * iw / 320) * 4, 4);
        }
        stbi_image_free(data);
        it = images.emplace(path, std::move(scaled)).first;
    }
    if (it->second.empty()) return;
    for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col) {
        int px = x + col, py = y + row;
        if (px < 0 || py < 0 || px >= width || py >= height) continue;
        auto p = &it->second[((row * 320 / h) * 320 + col * 320 / w) * 4];
        pixels[py * width + px] = blend(pixels[py * width + px], uint32_t(p[0]) << 16 | p[1] << 8 | p[2], p[3] * alpha / 255);
    }
}
void Canvas::draw(const std::vector<Entry>& entries, System system, size_t selected, const std::string& notice, bool about) {
    clear();
    text("USCL", 100, 102, 54);
    text("Native PS4 preview  |  9.00 / GoldHEN target", 1170, 95, 24, 0xd8c9eb, 650);
    const char* names[] = {"All games", "PS4", "PS3", "Switch", "Emulators"};
    const char* icons[] = {"cat_game.png", "cat_ps4.png", "cat_ps3.png", "cat_switch.png", "cat_settings.png"};
    for (int i = 0; i < 5; ++i) {
        int x = 160 + i * 330;
        image(assets + "/" + icons[i], x, 150, 95, 95, i == int(system) ? 255 : 130);
        text(names[i], x - 15, 285, 30, i == int(system) ? 0xffffff : 0xab99bc, 300);
        if (i == int(system)) rect(x - 12, 300, 180, 3, 0xe4caff);
    }
    if (about) {
        text("USCL on PlayStation 4", 170, 420, 46);
        const char* lines[] = {
            "This native preview launches installed PS4 games and emulator applications.",
            "PS3 games: /data/rpcs3/games or /data/rpcs3/dev_hdd0/game (RPCS30010).",
            "Switch games: /data/edenps4/roms (NX on Orbis, EDPS00001).",
            "These experimental ports have game, firmware and performance limitations.",
            "PC Steam lobbies and USCL online services are not ported.",
            "Use your own games and emulator files. Press Circle to return."
        };
        int y = 500; for (auto s : lines) { text(s, 170, y, 27); y += 62; }
    } else {
        auto visible = filter(entries, system);
        if (visible.empty()) {
            text(system == System::PS3 ? "No installed RPCS3 PS4 application found" :
                 system == System::Switch ? "No installed Eden / NX on Orbis application found" : "No installed games found", 220, 480, 42);
            text("Triangle: rescan    Options: setup and compatibility", 220, 560, 29, 0xd9c9ec);
        } else {
            selected = std::min(selected, visible.size() - 1);
            const auto& e = entries[visible[selected]];
            image(assets + (e.system==System::PS3 ? "/cat_ps3.png" : e.system==System::Switch ? "/cat_switch.png" : "/cat_ps4.png"), 240, 430, 200, 200, 130);
            if (!e.metadata.empty()) image(e.metadata + "/icon0.png", 200, 390, 280, 280);
            text(e.title, 540, 465, 46, 0xffffff, 1230);
            text(e.id + (!e.gamePath.empty() ? "  |  Local game via native emulator" : e.system == System::PS4 ? "  |  Installed PS4 application" : "  |  Opens native emulator"), 540, 525, 27, 0xd8c9eb, 1230);
            text("Cross: launch on this PS4", 540, 590, 30);
            int y = 740;
            for (size_t i = selected + 1; i < visible.size() && i < selected + 4; ++i) {
                text(entries[visible[i]].title, 220, y, 30, 0xc1b0d3, 1500); y += 65;
            }
            text(std::to_string(selected + 1) + " / " + std::to_string(visible.size()), 1660, 925, 28, 0xd8c9eb, 200);
        }
    }
    if (!notice.empty()) text(notice, 100, 977, 25, 0xffdfa3, 1720);
    text("D-pad: browse     Cross: launch     Triangle: rescan     Options: help     Circle: exit", 100, 1040, 25, 0xd8c9eb, 1720);
}
}
