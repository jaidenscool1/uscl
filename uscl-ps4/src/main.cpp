#include "platform.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv) {
    using namespace uscl;
    setvbuf(stdout,nullptr,_IONBF,0);
    std::string assets="/app0/assets", error, notice;
#ifdef USCL_HOST
    assets="build/stage/assets";
#endif
    Platform platform;
    if (!platform.initialize(error)) {
        std::fprintf(stderr,"USCL startup: %s\n",error.c_str());
        platform.finish(); return 1;
    }
    Canvas canvas(assets);
    if (!canvas.fontReady()) { std::fprintf(stderr,"USCL: packaged font could not be loaded\n"); platform.finish(); return 1; }
    Scan library=platform.scan(error);
    auto scanNotice=[&] {
        if (!error.empty()) return error;
        return std::to_string(library.entries.size())+" installed applications | "+std::to_string(library.invalid)+" invalid metadata records";
    };
    notice=scanNotice();
    System system=System::All;
    size_t selected[5]{};
    bool about=false, quit=false;
#ifdef USCL_HOST
    if (argc==3 && !std::strcmp(argv[1],"--capture")) {
        canvas.draw(library.entries,System::PS3,0,"Desktop rendering fixture. Console execution has not been tested.",false);
        FILE* out=std::fopen(argv[2],"wb");
        if (!out) return 1;
        std::fprintf(out,"P6\n%d %d\n255\n",width,height);
        for (auto p:canvas.pixels) { uint8_t rgb[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)}; if (std::fwrite(rgb,1,3,out)!=3) { std::fclose(out); return 1; } }
        return std::fclose(out)==0 ? 0 : 1;
    }
#else
    (void)argc; (void)argv;
#endif
    bool dirty=true;
    while (!quit) {
        uint32_t buttons=platform.input();
        if (buttons) dirty=true;
        if (buttons&Back) { if (about) about=false; else quit=true; }
        if (buttons&Help) about=!about;
        if (!about) {
            if ((buttons&Left) && int(system)>0) system=System(int(system)-1);
            if ((buttons&Right) && int(system)<4) system=System(int(system)+1);
            auto visible=filter(library.entries,system);
            auto& index=selected[int(system)];
            if ((buttons&Up) && index) --index;
            if ((buttons&Down) && index+1<visible.size()) ++index;
            if (buttons&PageUp) index=index>5 ? index-5 : 0;
            if ((buttons&PageDown) && !visible.empty()) index=std::min(index+5,visible.size()-1);
            if ((buttons&Accept) && !visible.empty()) {
                error.clear();
                if (platform.launch(library.entries[visible[index]],error)) notice="Launch request accepted by the PS4";
                else notice=error;
            }
            if (buttons&Refresh) {
                error.clear(); auto refreshed=platform.scan(error);
                // Keep the existing library on a service failure so a failed rescan does not erase it.
                if (error.empty()) { library=std::move(refreshed); for (auto& i:selected) i=0; }
                notice=scanNotice();
            }
        }
        if (dirty) { canvas.draw(library.entries,system,selected[int(system)],notice,about); dirty=false; }
        if (!platform.present(canvas.pixels)) { std::fprintf(stderr,"USCL display presentation failed\n"); break; }
    }
    platform.finish(); return 0;
}
