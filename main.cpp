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

struct ConstantBuffer
{
    XMMATRIX mvp;
};

SDL_Window* window = nullptr;
IDXGIFactory7* factory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12Device14* device = nullptr;
ID3D12Resource* renderTargets[FRAME_COUNT] = {};
ID3D12CommandAllocator* cmdAlloc[FRAME_COUNT] = {};
ID3D12CommandQueue* cmdQueue = nullptr;
ID3D12RootSignature* rootSignature = nullptr;
ID3D12DescriptorHeap* rtvHeap = nullptr;
ID3D12PipelineState* pipelineState = nullptr;
ID3D12GraphicsCommandList* cmdList = nullptr;
UINT rtvDescriptorSize = 0;

ID3D12Resource* vertexBuffer = nullptr;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
ID3D12Resource* indexBuffer = nullptr;
D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

ID3D12Resource* constantBuffer = nullptr;
UINT8* pCbvDataBegin = nullptr;
UINT alignedCBSize = 0;

ID3D12Resource* depthStencilBuffer = nullptr;
ID3D12DescriptorHeap* dsvHeap = nullptr;

UINT frameIndex = 0;
HANDLE fenceEvent = 0;
ID3D12Fence *fence = nullptr;
UINT64 frameFenceValues[FRAME_COUNT] = {};
UINT64 fenceValue = 1;

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

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));

    D3D12_HEAP_PROPERTIES depthHeapProps = {};
    depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depthResDesc = {};
    depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthResDesc.Width = WINDOW_WIDTH;
    depthResDesc.Height = WINDOW_HEIGHT;
    depthResDesc.DepthOrArraySize = 1;
    depthResDesc.MipLevels = 1;
    depthResDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthResDesc.SampleDesc.Count = 1;
    depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;

    device->CreateCommittedResource(&depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthResDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&depthStencilBuffer));
    device->CreateDepthStencilView(depthStencilBuffer, nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc[i]));
    }
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc[0], nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;     
    rootSigDesc.pParameters = &rootParameters[0]; 
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signatureBlob = nullptr;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, nullptr);

    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

    signatureBlob->Release();
    signatureBlob = nullptr;

    size_t vsSize = 0, psSize = 0;
    void* vsData = SDL_LoadFile("shaders/dxil/VertexShader.dxil", &vsSize);
    void* psData = SDL_LoadFile("shaders/dxil/PixelShader.dxil", &psSize);

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vsData, vsSize };
    psoDesc.PS = { psData, psSize };
    psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP, D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

    SDL_free(vsData);
    vsData = nullptr;
    SDL_free(psData);
    psData = nullptr;

    Vertex vertices[] =
    {
        { {-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f, 1.0f} },
        { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f, 1.0f} }, 
        { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f, 1.0f} }, 
        { { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f, 1.0f} },
        { {-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f, 1.0f} },
        { {-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 1.0f, 1.0f} },
        { { 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f, 1.0f} }, 
        { { 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 0.0f, 1.0f} }
    };
    const UINT vertexBufferSize = sizeof(vertices);

    uint16_t indices[] = 
    {
        0, 1, 2,  0, 2, 3,
        4, 6, 5,  4, 7, 6,
        4, 5, 1,  4, 1, 0,
        3, 2, 6,  3, 6, 7,
        1, 5, 6,  1, 6, 2,
        4, 0, 3,  4, 3, 7 
    };
    const UINT indexBufferSize = sizeof(indices);

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RANGE readRange = { 0, 0 };

    D3D12_RESOURCE_DESC vertexBufferDesc = {};
    vertexBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexBufferDesc.Width = vertexBufferSize;
    vertexBufferDesc.Height = 1;
    vertexBufferDesc.DepthOrArraySize = 1;
    vertexBufferDesc.MipLevels = 1;
    vertexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    vertexBufferDesc.SampleDesc.Count = 1;
    vertexBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_DESC indexBufferDesc = {};
    indexBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    indexBufferDesc.Width = indexBufferSize;
    indexBufferDesc.Height = 1;
    indexBufferDesc.DepthOrArraySize = 1;
    indexBufferDesc.MipLevels = 1;
    indexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexBufferDesc.SampleDesc.Count = 1;
    indexBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_DESC tempUploadDesc = {};
    tempUploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    tempUploadDesc.Width = indexBufferSize + vertexBufferSize;
    tempUploadDesc.Height = 1;
    tempUploadDesc.DepthOrArraySize = 1;
    tempUploadDesc.MipLevels = 1;
    tempUploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    tempUploadDesc.SampleDesc.Count = 1;
    tempUploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer));
    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &indexBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&indexBuffer));

    ID3D12Resource* tempUploadBuffer = nullptr;
    device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &tempUploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tempUploadBuffer));

    UINT8* pMappedData = nullptr;
    tempUploadBuffer->Map(0, nullptr, (void**)(&pMappedData));
    memcpy(pMappedData, vertices, vertexBufferSize);
    memcpy(pMappedData + vertexBufferSize, indices, indexBufferSize);
    tempUploadBuffer->Unmap(0, nullptr);

    cmdAlloc[frameIndex]->Reset();
    cmdList->Reset(cmdAlloc[frameIndex], nullptr);
    
    if (tempUploadBuffer != nullptr)
    {
        cmdList->CopyBufferRegion(vertexBuffer, 0, tempUploadBuffer, 0, vertexBufferSize);
        cmdList->CopyBufferRegion(indexBuffer, 0, tempUploadBuffer, vertexBufferSize, indexBufferSize);
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = vertexBuffer;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = indexBuffer;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(2, barriers);
    cmdList->Close();

    ID3D12CommandList* ppCmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, ppCmdLists);

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceValue = 1;
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    const UINT64 fenceToWaitFor = fenceValue;
    cmdQueue->Signal(fence, fenceToWaitFor);
    fenceValue++;

    if (fence->GetCompletedValue() < fenceToWaitFor)
    {
        fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
        if (fenceEvent != nullptr)
        {
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    tempUploadBuffer->Release();
    tempUploadBuffer = nullptr;

    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;

    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = indexBufferSize;
    indexBufferView.Format = DXGI_FORMAT_R16_UINT;

    alignedCBSize = (sizeof(ConstantBuffer) + 255) & ~255;
    UINT totalCBSize = alignedCBSize * FRAME_COUNT;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = totalCBSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer));

    constantBuffer->Map(0, &readRange, (void**)(&pCbvDataBegin));

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    if (fence->GetCompletedValue() < frameFenceValues[frameIndex])
    {
        fence->SetEventOnCompletion(frameFenceValues[frameIndex], fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    cmdAlloc[frameIndex]->Reset();
    cmdList->Reset(cmdAlloc[frameIndex], pipelineState);

    static float angle = 0.0f;
    angle += 0.01f;

    XMMATRIX mModel = XMMatrixRotationY(angle) * XMMatrixRotationX(0.0f) * XMMatrixRotationZ(0.0f);
    XMMATRIX mView = XMMatrixLookAtLH(XMVectorSet(0.0f, 1.5f, -3.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.1f, 100.0f);

    ConstantBuffer constBufferData;
    constBufferData.mvp = XMMatrixTranspose(mModel * mView * mProj);
    UINT8* destinationPointer = pCbvDataBegin + (frameIndex * alignedCBSize);
    memcpy(destinationPointer, &constBufferData, sizeof(constBufferData));

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, WINDOW_WIDTH, WINDOW_HEIGHT, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets[frameIndex];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * rtvDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->SetGraphicsRootSignature(rootSignature);
    D3D12_GPU_VIRTUAL_ADDRESS cbGpuAddress = constantBuffer->GetGPUVirtualAddress() + (frameIndex * alignedCBSize);
    cmdList->SetGraphicsRootConstantBufferView(0, cbGpuAddress);
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
    cmdList->IASetIndexBuffer(&indexBufferView);
    cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->Close();
    ID3D12CommandList* ppCmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, ppCmdLists);

    swapChain->Present(1, 0);

    const UINT64 currentFenceValue = fenceValue;
    cmdQueue->Signal(fence, currentFenceValue);
    frameFenceValues[frameIndex] = currentFenceValue;
    fenceValue++;

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
    if (cmdQueue and fence and fenceEvent)
    {
        const UINT64 fenceToWaitFor = fenceValue;
        cmdQueue->Signal(fence, fenceToWaitFor);
        fenceValue++;

        if (fence->GetCompletedValue() < fenceToWaitFor)
        {
            fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    if (fenceEvent != NULL)
    {
        CloseHandle(fenceEvent);
        fenceEvent = NULL;
    }

    fence->Release();
    fence = nullptr;

    constantBuffer->Unmap(0, nullptr);
    constantBuffer->Release();
    constantBuffer = nullptr;
    pCbvDataBegin = nullptr;

    indexBuffer->Release();
    indexBuffer = nullptr;

    vertexBuffer->Release(); 
    vertexBuffer = nullptr;
    
    depthStencilBuffer->Release();
    depthStencilBuffer = nullptr;

    dsvHeap->Release();
    dsvHeap = nullptr;

    pipelineState->Release();
    pipelineState = nullptr;
    
    rootSignature->Release();
    rootSignature = nullptr;

    cmdList->Release();   
    cmdList = nullptr;

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        cmdAlloc[i]->Release();
        cmdAlloc[i] = nullptr;
    }
    

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        if (renderTargets[i])
        {
            renderTargets[i]->Release();
            renderTargets[i] = nullptr;
        }
    }

    rtvHeap->Release();
    rtvHeap = nullptr;
    
    swapChain->Release();
    swapChain = nullptr;

    cmdQueue->Release();
    cmdQueue = nullptr;

    device->Release();
    device = nullptr;

    factory->Release();
    factory = nullptr;

    SDL_DestroyWindow(window);
    window = nullptr;

    SDL_Quit();
}