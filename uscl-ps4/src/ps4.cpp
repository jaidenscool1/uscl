#include "platform.hpp"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <libjbc.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

extern "C" int sceLncUtilInitialize(void);
namespace uscl {
struct Platform::State {
    int video=-1, pad=-1, user=-1, current=0, appModule=-1;
    int (*appExists)(const char*, int*) = nullptr;
    int64_t frame=0;
    off_t memoryOffset=0;
    void* memory=nullptr;
    size_t memorySize=0;
    void* buffers[2]{};
    jbc_cred original{};
    bool restorePending=false;
    uint32_t previous=0;
};
namespace {
bool check(int result, const char* action, std::string& error) {
    if (result >= 0) return true;
    char msg[180]; std::snprintf(msg, sizeof(msg), "%s: 0x%08X", action, unsigned(result));
    error=msg; std::fprintf(stderr, "%s\n", msg); return false;
}
}
Platform::Platform() : state(new State) {}
Platform::~Platform() {
    auto& s=*state;
    if (s.pad>=0) scePadClose(s.pad);
    if (s.video>=0) sceVideoOutClose(s.video);
    if (s.memory) sceKernelMunmap(s.memory,s.memorySize);
    if (s.memorySize) sceKernelReleaseDirectMemory(s.memoryOffset,s.memorySize);
    if (s.restorePending) jbc_set_cred(&s.original);
}
bool Platform::initialize(std::string& error) {
    auto& s=*state;
    // GoldHEN supplies the homebrew kernel interface used by libjbc. No firmware offsets are embedded here.
    if (!check(jbc_get_cred(&s.original),"Read homebrew credentials",error)) return false;
    auto credentials=s.original;
    if (!check(jbc_jailbreak_cred(&credentials),"Prepare homebrew credentials",error)) return false;
    s.restorePending=true;
    if (!check(jbc_set_cred(&credentials),"Apply homebrew credentials",error)) return false;
    mkdir("/data/uscl",0755);
    // Keep a single local log, with no account IDs or network credentials.
    if (FILE* log=std::freopen("/data/uscl/startup.log","w",stderr)) setvbuf(log,nullptr,_IONBF,0);
    std::fprintf(stderr,"USCL native preview 00.01; target 9.00 / GoldHEN; runtime compatibility unverified\n");
    if (!check(sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL),"Load font service",error)) return false;
    // An already initialized user service is normal when another homebrew loaded it first.
    sceUserServiceInitialize(nullptr);
    if (!check(sceUserServiceGetForegroundUser(&s.user),"Get signed-in user",error)) return false;
    if (s.user<0) { error="Sign in to a PS4 user before opening USCL"; return false; }
    if (!check(scePadInit(),"Initialize controller service",error)) return false;
    s.pad=scePadOpen(s.user,0,0,nullptr);
    if (!check(s.pad,"Open controller",error)) return false;
    s.video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,nullptr);
    if (!check(s.video,"Open display",error)) return false;
    constexpr size_t frameBytes=width*height*4;
    s.memorySize=(frameBytes*2+0x1fffff)&~size_t(0x1fffff);
    if (!check(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),s.memorySize,0x200000,3,&s.memoryOffset),"Allocate display buffers",error)) { s.memorySize=0; return false; }
    if (!check(sceKernelMapDirectMemory(&s.memory,s.memorySize,0x33,0,s.memoryOffset,0x200000),"Map display buffers",error)) return false;
    s.buffers[0]=s.memory; s.buffers[1]=static_cast<char*>(s.memory)+frameBytes;
    OrbisVideoOutBufferAttribute attr{};
    sceVideoOutSetBufferAttribute(&attr,0x80000000,1,0,width,height,width);
    if (!check(sceVideoOutRegisterBuffers(s.video,0,s.buffers,2,&attr),"Register display buffers",error)) return false;
    sceVideoOutSetFlipRate(s.video,0);
    sceSystemServiceHideSplashScreen();
    return true;
}
Scan Platform::scan(std::string& error) {
    auto& s=*state;
    if (!s.appExists) {
        s.appModule=sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx",0,nullptr,0,nullptr,nullptr);
        if (!check(s.appModule,"Load installed-app service",error)) return {};
        int (*initialize)()=nullptr;
        if (!check(sceKernelDlsym(s.appModule,"sceAppInstUtilInitialize",reinterpret_cast<void**>(&initialize)),"Resolve installed-app service",error)) return {};
        if (!check(sceKernelDlsym(s.appModule,"sceAppInstUtilAppExists",reinterpret_cast<void**>(&s.appExists)),"Resolve installed-app query",error)) return {};
        if (!initialize || !s.appExists) { error="Installed-app service returned an empty export"; s.appExists=nullptr; return {}; }
        int r=initialize();
        if (r<0) { s.appExists=nullptr; check(r,"Initialize installed-app service",error); return {}; }
    }
    unsigned checked=0, failures=0;
    auto result=discover({"/system_data/priv/appmeta","/system_data/priv/appmeta/external","/user/appmeta","/user/appmeta/external"},[&](const std::string& id) {
        int exists=0, r=s.appExists(id.c_str(),&exists); ++checked;
        if (r<0) ++failures;
        return r>=0 && exists!=0;
    });
    if (checked && checked==failures) error="Installed-app queries failed; verify GoldHEN and connected storage";
    else if (!result.rootsOpened) error="Cannot read installed-app metadata; verify GoldHEN";
    if (error.empty()) discoverEmulatedGames(result,
        {"/data/rpcs3/games","/data/rpcs3/dev_hdd0/game","/mnt/usb0/PS3/GAMES","/mnt/usb1/PS3/GAMES"},
        {"/data/edenps4/roms","/mnt/usb0/Switch","/mnt/usb1/Switch"});
    std::fprintf(stderr,"Library: %zu entries, %u roots, %u invalid metadata, %u unreadable records/roots\n",result.entries.size(),result.rootsOpened,result.invalid,result.unreadable);
    return result;
}
uint32_t Platform::input() {
    auto& s=*state;
    int user=-1;
    if (sceUserServiceGetForegroundUser(&user)>=0 && user>=0 && user!=s.user) {
        if (s.pad>=0) scePadClose(s.pad);
        s.user=user; s.pad=scePadOpen(user,0,0,nullptr); s.previous=0;
    }
    if (s.pad<0 && s.user>=0) s.pad=scePadOpen(s.user,0,0,nullptr);
    OrbisPadData data{};
    if (s.pad<0 || scePadReadState(s.pad,&data)<0 || !data.connected) { s.previous=0; return 0; }
    uint32_t now=data.buttons;
    if (data.leftStick.x<55) now|=ORBIS_PAD_BUTTON_LEFT;
    if (data.leftStick.x>200) now|=ORBIS_PAD_BUTTON_RIGHT;
    if (data.leftStick.y<55) now|=ORBIS_PAD_BUTTON_UP;
    if (data.leftStick.y>200) now|=ORBIS_PAD_BUTTON_DOWN;
    uint32_t pressed=now&~s.previous; s.previous=now;
    const uint32_t native[]={ORBIS_PAD_BUTTON_UP,ORBIS_PAD_BUTTON_DOWN,ORBIS_PAD_BUTTON_LEFT,ORBIS_PAD_BUTTON_RIGHT,
        ORBIS_PAD_BUTTON_CROSS,ORBIS_PAD_BUTTON_CIRCLE,ORBIS_PAD_BUTTON_TRIANGLE,ORBIS_PAD_BUTTON_OPTIONS,ORBIS_PAD_BUTTON_L1,ORBIS_PAD_BUTTON_R1};
    uint32_t result=0; for (int i=0;i<10;++i) if (pressed&native[i]) result|=1u<<i;
    return result;
}
bool Platform::present(const std::vector<uint32_t>& pixels) {
    auto& s=*state;
    if (!s.memory || pixels.size()!=size_t(width*height)) return false;
    std::memcpy(s.buffers[s.current],pixels.data(),pixels.size()*4);
    int64_t frame=++s.frame;
    if (sceVideoOutSubmitFlip(s.video,s.current,ORBIS_VIDEO_OUT_FLIP_VSYNC,frame)<0) return false;
    // Bounded wait: a failed display must not hang the application forever.
    bool complete=false;
    for (int i=0;i<1000;++i) {
        OrbisVideoOutFlipStatus status{};
        if (sceVideoOutGetFlipStatus(s.video,&status)<0) return false;
        if (status.flipArg==frame) { complete=true; break; }
        sceKernelUsleep(1000);
    }
    s.current^=1;
    return complete;
}
bool Platform::launch(const Entry& entry,std::string& error) {
    if (!validId(entry.id) || entry.id==titleId) { error="Invalid application title ID"; return false; }
    if (!check(int(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE)),"Load game launcher",error)) return false;
    int r=sceLncUtilInitialize();
    if (uint32_t(r)!=0x80940018u && !check(r,"Initialize game launcher",error)) return false;
    int user=-1;
    if (!check(sceUserServiceGetForegroundUser(&user),"Get launch user",error)) return false;
    if (user<0) { error="Sign in to a PS4 user before launching"; return false; }
    LncAppParam p{}; p.size=sizeof(p); p.user_id=user; p.LaunchAppCheck_flag=LaunchApp_SkipSystemUpdate;
    std::string bootTarget;
    std::vector<uint8_t> previousBoot;
    bool hadBoot=false;
    if (!entry.gamePath.empty()) {
        const char* target=entry.id=="RPCS30010" ? "/data/rpcs3/boot.txt" : entry.id=="EDPS00001" ? "/data/edenps4/game.txt" : nullptr;
        if (!target) { error="Unsupported emulator launch interface"; return false; }
        struct stat st{};
        hadBoot=lstat(target,&st)==0;
        if (hadBoot && (!S_ISREG(st.st_mode) || st.st_size>4096)) { error="Existing emulator launch file cannot be safely preserved"; return false; }
        if (hadBoot) {
            previousBoot=readFile(target,4096);
            if (previousBoot.size()!=size_t(st.st_size)) { error="Cannot preserve existing emulator launch file"; return false; }
        }
        if (!writeBootFile(target,entry.gamePath,error)) return false;
        bootTarget=target;
    }
    // The return value is an app ID on success, not necessarily zero.
    int result=sceLncUtilLaunchApp(entry.id.c_str(),nullptr,&p);
    if (result<0 && !bootTarget.empty()) {
        std::string rollback;
        if (hadBoot) { if (!writeFileAtomic(bootTarget,previousBoot,rollback)) error="; launch selection restoration failed"; }
        else if (unlink(bootTarget.c_str())) error="; launch selection cleanup failed";
    }
    std::string rollbackError=error;
    bool ok=check(result,"Launch application",error);
    if (!ok) error+=rollbackError;
    std::fprintf(stderr,"Launch %s: 0x%08X\n",entry.id.c_str(),unsigned(result));
    return ok;
}
void Platform::finish() {
    auto& s=*state;
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    if (s.restorePending && jbc_set_cred(&s.original)==0) s.restorePending=false;
    sceSystemServiceLoadExec("exit",nullptr);
    for (;;) sceKernelUsleep(1000000);
}
}
