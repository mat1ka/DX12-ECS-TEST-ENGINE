#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <climits>
#include <DirectXMath.h>

using namespace DirectX;

SDL_Window* window = nullptr;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("test", 800, 600, NULL);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    
    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    SDL_DestroyWindow(window);
    window = nullptr;

    SDL_Quit();
}