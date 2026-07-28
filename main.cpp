#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <climits>
#include <DirectXMath.h>

using namespace DirectX;

const UINT WINDOW_WIDTH = 800;
const UINT WINDOW_HEIGHT = 600;
const int FRAME_COUNT = 2;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

SDL_Window* window = nullptr;
IDXGIFactory7* factory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12Device14* device = nullptr;
ID3D12CommandAllocator* cmdAlloc[FRAME_COUNT] = {};
ID3D12CommandQueue* cmdQueue = nullptr;
ID3D12RootSignature* rootSignature = nullptr;
ID3D12DescriptorHeap* rtvHeap = nullptr;
ID3D12PipelineState* pipelineState = nullptr;
ID3D12GraphicsCommandList* cmdList = nullptr;
UINT rtvDescriptorSize = 0;

ID3D12Resource* vertexBuffer = nullptr;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

UINT frameIndex = 0;
HANDLE fenceEvent = {};
ID3D12Fence *fence = nullptr;
UINT64 fenceValue = 0;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("test", WINDOW_WIDTH, WINDOW_HEIGHT, NULL);

    IDXGIAdapter4* adapter = nullptr;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device));
    adapter->Release();
    adapter = nullptr;

    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
    cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Width = WINDOW_WIDTH;
    swapChainDesc.Height = WINDOW_HEIGHT;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    IDXGISwapChain1* tempSwapChain = nullptr;
    factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &swapChainDesc, nullptr, nullptr, &tempSwapChain);
    tempSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain));
    tempSwapChain->Release();
    tempSwapChain = nullptr;



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