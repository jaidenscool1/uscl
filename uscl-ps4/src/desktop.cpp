#include "platform.hpp"
#include <SDL.h>
#include <cstdio>

namespace uscl {
struct Platform::State { SDL_Window* window{}; SDL_Renderer* renderer{}; SDL_Texture* texture{}; };
Platform::Platform() : state(new State) {}
Platform::~Platform() {
    SDL_DestroyTexture(state->texture); SDL_DestroyRenderer(state->renderer); SDL_DestroyWindow(state->window); SDL_Quit();
}
bool Platform::initialize(std::string& error) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)) { error=SDL_GetError(); return false; }
    state->window=SDL_CreateWindow("USCL PS4 native UI preview",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
    if (!state->window) { error=SDL_GetError(); return false; }
    state->renderer=SDL_CreateRenderer(state->window,-1,SDL_RENDERER_SOFTWARE);
    if (!state->renderer) { error=SDL_GetError(); return false; }
    state->texture=SDL_CreateTexture(state->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
    if (!state->texture) { error=SDL_GetError(); return false; }
    SDL_RenderSetLogicalSize(state->renderer,width,height); return true;
}
Scan Platform::scan(std::string&) {
    // UI fixtures only. No PC-installed game is claimed to be a console installation.
    Scan s;
    s.entries={{"RPCS30010","RPCS3 PORT FOR PS4 (preview fixture)","",System::PS3,""},
               {"EDPS00001","NX on Orbis (preview fixture)","",System::Switch,""}};
    return s;
}
uint32_t Platform::input() {
    SDL_Event event; uint32_t result=0;
    while (SDL_PollEvent(&event)) {
        if (event.type==SDL_QUIT) result|=Back;
        if (event.type!=SDL_KEYDOWN) continue;
        switch (event.key.keysym.sym) {
        case SDLK_UP: result|=Up; break; case SDLK_DOWN: result|=Down; break;
        case SDLK_LEFT: result|=Left; break; case SDLK_RIGHT: result|=Right; break;
        case SDLK_RETURN: result|=Accept; break; case SDLK_ESCAPE: result|=Back; break;
        case SDLK_r: result|=Refresh; break; case SDLK_F1: result|=Help; break;
        case SDLK_PAGEUP: result|=PageUp; break; case SDLK_PAGEDOWN: result|=PageDown; break;
        }
    }
    return result;
}
bool Platform::present(const std::vector<uint32_t>& p) {
    if (SDL_UpdateTexture(state->texture,nullptr,p.data(),width*4)) return false;
    SDL_RenderClear(state->renderer); SDL_RenderCopy(state->renderer,state->texture,nullptr,nullptr); SDL_RenderPresent(state->renderer);
    SDL_Delay(16); return true;
}
bool Platform::launch(const Entry&,std::string& error) { error="Desktop preview: launching a PS4 application requires the console"; return false; }
void Platform::finish() {}
}
