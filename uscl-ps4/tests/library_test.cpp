#include "../src/library.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <unistd.h>

namespace fs=std::filesystem;
using namespace uscl;
void w16(std::vector<uint8_t>& b,size_t p,uint16_t n) { b[p]=n; b[p+1]=n>>8; }
void w32(std::vector<uint8_t>& b,size_t p,uint32_t n) { w16(b,p,n); w16(b,p+2,n>>16); }
std::vector<uint8_t> fixture(std::string id="CUSA00001",std::string title="Test game",std::string category="gd") {
    std::vector<std::pair<std::string,std::string>> values={{"TITLE_ID",id},{"TITLE",title},{"CATEGORY",category}};
    std::string keys; size_t size=20+16*values.size();
    for (auto& p:values) keys+=p.first+'\0';
    size_t data=size+keys.size(),total=data;
    for (auto& p:values) total+=p.second.size()+1;
    std::vector<uint8_t> b(total); std::memcpy(b.data(),"\0PSF",4);
    w32(b,4,0x101); w32(b,8,size); w32(b,12,data); w32(b,16,values.size());
    std::memcpy(b.data()+size,keys.data(),keys.size());
    size_t ko=0,vo=0;
    for (size_t i=0;i<values.size();++i) {
        auto& p=values[i]; size_t e=20+i*16,len=p.second.size()+1;
        w16(b,e,ko); w16(b,e+2,0x204); w32(b,e+4,len); w32(b,e+8,len); w32(b,e+12,vo);
        std::memcpy(b.data()+data+vo,p.second.c_str(),len); ko+=p.first.size()+1; vo+=len;
    }
    return b;
}
void save(const fs::path& p,const std::vector<uint8_t>& b) {
    fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size()); assert(f.good());
}
int main() {
    Entry e; auto good=fixture(); assert(parseSfo(good,e)); assert(e.id=="CUSA00001" && e.title=="Test game");
    assert(validId("USCL00001") && !validId("../passwd") && !validId("CUSA0000A") && !validId("CUSA000010"));
    for (size_t n=0;n<good.size();++n) { auto b=good; b.resize(n); assert(!parseSfo(b,e)); }
    for (size_t p:{size_t(8),size_t(12),size_t(16),size_t(24),size_t(28),size_t(32)}) {
        auto b=good; w32(b,p,0xffffffff); assert(!parseSfo(b,e));
    }
    { auto b=good; w16(b,20,0xffff); assert(!parseSfo(b,e)); }
    { auto b=good; w16(b,22,0x404); assert(!parseSfo(b,e)); }
    { auto b=good; w16(b,36,0); assert(!parseSfo(b,e)); }
    { auto b=good; auto data=b[12]|uint32_t(b[13])<<8; b[data+9]='!'; assert(!parseSfo(b,e)); }
    assert(!parseSfo(fixture("bad"),e));
    std::mt19937 random(42);
    for (int n=0;n<20000;++n) {
        auto b=good; for (int j=0;j<5;++j) b[random()%b.size()]=uint8_t(random());
        parseSfo(b,e); // Sanitizers check every mutated offset/length combination.
    }
    char temp[]="/tmp/uscl-ps4-test-XXXXXX"; auto* path=mkdtemp(temp); assert(path);
    fs::path root(path);
    save(root/"a/CUSA00001/param.sfo",fixture());
    save(root/"b/CUSA00001/param.sfo",fixture("CUSA00001","Duplicate"));
    save(root/"a/USCL00001/param.sfo",fixture("USCL00001"));
    save(root/"a/NPXS00001/param.sfo",fixture("NPXS00001"));
    save(root/"a/CUSA00002/param.sfo",fixture("CUSA00002","Not installed"));
    save(root/"a/CUSA00003/param.sfo",fixture("CUSA99999","Mismatched ID"));
    save(root/"a/RPCS30010/param.sfo",fixture("RPCS30010","RPCS3 PORT FOR PS4"));
    save(root/"a/EDPS00001/param.sfo",fixture("EDPS00001","NX on Orbis"));
    auto scan=discover({(root/"a").string(),(root/"b").string(),(root/"missing").string()},[](const std::string& id) { return id!="CUSA00002"; });
    assert(scan.entries.size()==3 && scan.invalid==1 && scan.rootsOpened==2);
    assert(filter(scan.entries,System::PS3).size()==1 && filter(scan.entries,System::Switch).size()==1);
    save(root/"ps3/disc/PS3_GAME/PARAM.SFO",fixture("BLUS00001","Disc game","DG"));
    save(root/"ps3/disc/PS3_GAME/USRDIR/EBOOT.BIN",{1});
    save(root/"ps3/update/PARAM.SFO",fixture("BLUS00002","Update only","GD"));
    save(root/"ps3/update/USRDIR/EBOOT.BIN",{1});
    save(root/"switch/Game.nsp",{1}); save(root/"switch/homebrew.NRO",{1}); save(root/"switch/prod.keys",{1});
    fs::create_directory_symlink(root/"switch",root/"switch/loop");
    discoverEmulatedGames(scan,{(root/"ps3").string()},{(root/"switch").string()});
    assert(scan.entries.size()==6 && filter(scan.entries,System::PS3).size()==2 && filter(scan.entries,System::Switch).size()==3);
    auto boot=(root/"boot.txt").string(), game=(root/"switch/Game.nsp").string(); std::string error;
    assert(writeBootFile(boot,game,error));
    auto contents=readFile(boot); assert(std::string(contents.begin(),contents.end())==game+"\n");
    assert(!writeBootFile(boot,"/nonexistent/game",error)); assert(readFile(boot)==contents);
    assert(!writeBootFile(boot,game+"\n/another",error)); assert(readFile(boot)==contents);
    assert(!writeBootFile((root/"absent/boot.txt").string(),game,error));
    assert(readFile(game,0).empty());
    fs::remove_all(root);
    std::cout<<"PASS: SFO truncations, bounds, 20,000 mutations, installed filtering, deduplication, native ROM discovery and atomic handoff\n";
}
