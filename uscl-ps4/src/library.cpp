#include "library.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace uscl {
namespace {
uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
bool range(size_t off, size_t len, size_t size) { return off <= size && len <= size - off; }
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
bool validId(const std::string& id) {
    if (id.size() != 9) return false;
    for (size_t i = 0; i < id.size(); ++i)
        if (i < 4 ? id[i] < 'A' || id[i] > 'Z' : id[i] < '0' || id[i] > '9') return false;
    return true;
}
std::vector<uint8_t> readFile(const std::string& path, size_t limit) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::vector<uint8_t> data;
    uint8_t block[4096];
    size_t n;
    while ((n = std::fread(block, 1, sizeof(block), f)) != 0) {
        if (n > limit - data.size()) { std::fclose(f); return {}; }
        data.insert(data.end(), block, block + n);
    }
    bool ok = !std::ferror(f);
    std::fclose(f);
    return ok ? data : std::vector<uint8_t>{};
}
bool parseSfo(const std::vector<uint8_t>& b, Entry& out) {
    if (b.size() < 20 || std::memcmp(b.data(), "\0PSF", 4) || u32(b.data() + 4) != 0x101) return false;
    const size_t keys = u32(b.data() + 8), values = u32(b.data() + 12), count = u32(b.data() + 16);
    if (count > (b.size() - 20) / 16 || keys < 20 + count * 16 || values < keys || values > b.size()) return false;
    Entry result;
    bool haveId = false, haveTitle = false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* e = b.data() + 20 + i * 16;
        const size_t key = u16(e), length = u32(e + 4), capacity = u32(e + 8), offset = u32(e + 12);
        if (!range(key, 1, values - keys) || length > capacity || !range(offset, capacity, b.size() - values)) return false;
        const char* name = reinterpret_cast<const char*>(b.data() + keys + key);
        if (!std::memchr(name, 0, values - keys - key)) return false;
        bool id = !std::strcmp(name, "TITLE_ID"), title = !std::strcmp(name, "TITLE");
        if (!id && !title) continue;
        if (u16(e + 2) != 0x204 || !length || (id ? haveId : haveTitle)) return false;
        const char* text = reinterpret_cast<const char*>(b.data() + values + offset);
        const char* end = static_cast<const char*>(std::memchr(text, 0, length));
        if (!end) return false;
        if (id) { result.id.assign(text, end); haveId = true; }
        else { result.title.assign(text, end); haveTitle = true; }
    }
    if (!haveId || !haveTitle || !validId(result.id) || result.title.empty() || result.title.size() > 512) return false;
    for (auto& c : result.title) if (static_cast<unsigned char>(c) < 32) c = ' ';
    result.system = classify(result.title);
    out = std::move(result);
    return true;
}
System classify(const std::string& title) {
    auto t = lower(title);
    if (t.find("rpcs3") != std::string::npos) return System::PS3;
    if (t.find("eden") != std::string::npos || t.find("nx on orbis") != std::string::npos ||
        t.find("nx-on-orbis") != std::string::npos) return System::Switch;
    if (t.find("retroarch") != std::string::npos || t.find("dolphin") != std::string::npos ||
        t.find("ppsspp") != std::string::npos || t.find("azahar") != std::string::npos) return System::Other;
    return System::PS4;
}
Scan discover(const std::vector<std::string>& roots, const std::function<bool(const std::string&)>& installed) {
    Scan scan;
    std::map<std::string, Entry> found;
    for (const auto& root : roots) {
        DIR* dir = opendir(root.c_str());
        if (!dir) { ++scan.unreadable; continue; }
        ++scan.rootsOpened;
        while (auto* d = readdir(dir)) {
            std::string id = d->d_name;
            if (!validId(id) || id == titleId || id.compare(0, 4, "NPXS") == 0 || found.count(id)) continue;
            std::string path = root + "/" + id;
            auto bytes = readFile(path + "/param.sfo");
            if (bytes.empty()) { ++scan.unreadable; continue; }
            Entry e;
            if (!parseSfo(bytes, e) || e.id != id) { ++scan.invalid; continue; }
            if (!installed(id)) continue;
            e.metadata = path;
            found.emplace(id, std::move(e));
        }
        closedir(dir);
    }
    for (auto& e : found) scan.entries.push_back(std::move(e.second));
    std::sort(scan.entries.begin(), scan.entries.end(), [](const Entry& a, const Entry& b) {
        auto x = lower(a.title), y = lower(b.title);
        return x == y ? a.id < b.id : x < y;
    });
    return scan;
}
std::vector<size_t> filter(const std::vector<Entry>& entries, System system) {
    std::vector<size_t> result;
    for (size_t i = 0; i < entries.size(); ++i)
        if (system == System::All || entries[i].system == system) result.push_back(i);
    return result;
}
void discoverEmulatedGames(Scan& scan, const std::vector<std::string>& ps3Roots, const std::vector<std::string>& switchRoots) {
    // Handoffs are pinned to the application identities whose source interfaces were checked.
    bool ps3=false, nx=false;
    for (auto& e:scan.entries) { ps3|=e.id=="RPCS30010"; nx|=e.id=="EDPS00001"; }
    std::set<std::string> seen;
    auto regular=[](const std::string& path) { struct stat st{}; return lstat(path.c_str(),&st)==0 && S_ISREG(st.st_mode); };
    auto addPs3=[&](const std::string& directory) {
        std::string path=directory, metadata=path;
        if (!regular(path+"/PARAM.SFO")) { path+="/PS3_GAME"; metadata=path; }
        if (!regular(path+"/USRDIR/EBOOT.BIN")) return false;
        Entry e;
        const auto bytes=readFile(path+"/PARAM.SFO");
        if (!parseSfo(bytes,e)) return false;
        // Reject updates and game data: only disc games (DG) and bootable HDD games (HG).
        bool bootable=false;
        if (bytes.size()>=20) {
            size_t keys=u32(bytes.data()+8), values=u32(bytes.data()+12), count=u32(bytes.data()+16);
            for (size_t i=0;i<count;++i) {
                auto p=bytes.data()+20+i*16;
                const char* key=reinterpret_cast<const char*>(bytes.data()+keys+u16(p));
                if (!std::strcmp(key,"CATEGORY") && u16(p+2)==0x204 && u32(p+4)>=3) {
                    auto v=bytes.data()+values+u32(p+12);
                    bootable=(!std::memcmp(v,"DG\0",3) || !std::memcmp(v,"HG\0",3));
                }
            }
        }
        if (!bootable) return false;
        if (!seen.insert(path).second) return true;
        e.metadata=metadata; e.gamePath=directory; e.id="RPCS30010"; e.system=System::PS3;
        scan.entries.push_back(std::move(e)); return true;
    };
    std::function<void(const std::string&,int,bool)> walk;
    unsigned visited=0;
    walk=[&](const std::string& root,int depth,bool isPs3) {
        if (++visited>20000) return; // Bound work on unexpectedly large or adversarial directory trees.
        if (isPs3 && addPs3(root)) return;
        DIR* dir=opendir(root.c_str()); if (!dir) return;
        while (auto* d=readdir(dir)) {
            std::string name=d->d_name;
            if (name.empty() || name[0]=='.') continue;
            std::string path=root+"/"+name;
            struct stat st{};
            if (lstat(path.c_str(),&st)) continue;
            if (S_ISDIR(st.st_mode) && depth>0) walk(path,depth-1,isPs3);
            else if (S_ISREG(st.st_mode)) {
                auto pos=name.find_last_of('.'); if (pos==std::string::npos) continue;
                auto ext=lower(name.substr(pos));
                bool supported=isPs3 ? ext==".iso" : ext==".nsp" || ext==".xci" || ext==".nro";
                if (!supported || !seen.insert(path).second || path.find_first_of("\r\n")!=std::string::npos) continue;
                Entry e; e.id=isPs3 ? "RPCS30010" : "EDPS00001"; e.title=name.substr(0,pos);
                e.system=isPs3 ? System::PS3 : System::Switch; e.gamePath=path;
                scan.entries.push_back(std::move(e));
            }
        }
        closedir(dir);
    };
    if (ps3) for (const auto& root:ps3Roots) walk(root,6,true);
    if (nx) for (const auto& root:switchRoots) walk(root,6,false);
    std::sort(scan.entries.begin(),scan.entries.end(),[](const Entry& a,const Entry& b) {
        auto x=lower(a.title),y=lower(b.title); return x==y ? a.gamePath<b.gamePath : x<y;
    });
}
bool writeBootFile(const std::string& target,const std::string& game,std::string& error) {
    if (game.empty() || game.size()>4000 || game[0]!='/' || game.find_first_of("\r\n")!=std::string::npos) { error="Invalid console game path"; return false; }
    struct stat st{};
    if (lstat(game.c_str(),&st) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) { error="Game is missing or its storage is disconnected"; return false; }
    std::string contents=game+"\n";
    return writeFileAtomic(target,std::vector<uint8_t>(contents.begin(),contents.end()),error);
}
bool writeFileAtomic(const std::string& target,const std::vector<uint8_t>& contents,std::string& error) {
    std::string temporary=target+".uscl-XXXXXX";
    std::vector<char> name(temporary.begin(),temporary.end()); name.push_back(0);
    int fd=mkstemp(name.data());
    if (fd<0) { error="Cannot create emulator launch file"; return false; }
    size_t offset=0; bool ok=true;
    while (offset<contents.size()) {
        ssize_t n=write(fd,contents.data()+offset,contents.size()-offset);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { ok=false; break; } offset+=size_t(n);
    }
    if (ok && fsync(fd)) ok=false;
    if (ok && fchmod(fd,0644)) ok=false;
    if (close(fd)) ok=false;
    if (ok && rename(name.data(),target.c_str())) ok=false;
    if (!ok) { unlink(name.data()); error="Cannot save emulator launch file"; }
    return ok;
}
}
