#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uscl {
constexpr const char* titleId = "USCL00001";
enum class System { All, PS4, PS3, Switch, Other };
struct Entry {
    std::string id, title, metadata;
    System system = System::PS4;
    std::string gamePath;
};
struct Scan {
    std::vector<Entry> entries;
    unsigned unreadable = 0, invalid = 0, rootsOpened = 0;
};
bool validId(const std::string& id);
bool parseSfo(const std::vector<uint8_t>& bytes, Entry& entry);
Scan discover(const std::vector<std::string>& roots,
              const std::function<bool(const std::string&)>& installed);
void discoverEmulatedGames(Scan& scan, const std::vector<std::string>& ps3Roots, const std::vector<std::string>& switchRoots);
bool writeBootFile(const std::string& target, const std::string& game, std::string& error);
bool writeFileAtomic(const std::string& target, const std::vector<uint8_t>& bytes, std::string& error);
System classify(const std::string& title);
std::vector<size_t> filter(const std::vector<Entry>& entries, System system);
std::vector<uint8_t> readFile(const std::string& path, size_t limit = 1024 * 1024);
}
