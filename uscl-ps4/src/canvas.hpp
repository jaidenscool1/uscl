#pragma once
#include "library.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#ifndef USCL_HOST
#include <proto-include.h>
#endif
#include <map>

namespace uscl {
constexpr int width = 1920, height = 1080;
class Canvas {
    FT_Library ft{};
    FT_Face face{};
    std::map<std::string, std::vector<uint8_t>> images;
    std::string assets;
public:
    std::vector<uint32_t> pixels;
    explicit Canvas(const std::string& path);
    ~Canvas();
    bool fontReady() const { return face != nullptr; }
    void clear();
    void rect(int x, int y, int w, int h, uint32_t color);
    void text(const std::string& s, int x, int baseline, int size, uint32_t color = 0xffffff, int maxWidth = 1600);
    void image(const std::string& path, int x, int y, int w, int h, int alpha = 255);
    void draw(const std::vector<Entry>& entries, System system, size_t selected, const std::string& notice, bool about);
};
}
