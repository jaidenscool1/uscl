#pragma once
#include "canvas.hpp"
#include <memory>

namespace uscl {
enum Button : uint32_t { Up=1, Down=2, Left=4, Right=8, Accept=16, Back=32, Refresh=64, Help=128, PageUp=256, PageDown=512 };
class Platform {
    struct State;
    std::unique_ptr<State> state;
public:
    Platform();
    ~Platform();
    bool initialize(std::string& error);
    Scan scan(std::string& error);
    uint32_t input();
    bool present(const std::vector<uint32_t>& pixels);
    bool launch(const Entry& entry, std::string& error);
    void finish();
};
}
