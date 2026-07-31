#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <climits>
#include <DirectXMath.h>

using namespace DirectX;

const UINT WINDOW_WIDTH = 800;
const UINT WINDOW_HEIGHT = 600;
const int FRAME_COUNT = 2;
const UINT INSTANCE_COUNT = 5;
UINT currentInstanceCount = 1;

struct ComputeConstants
{
    XMMATRIX viewProj;
    float time;
    UINT totalInstances;
    UINT padding[2];
};

struct alignas(void*) MeshShaderPipelineStateStream
{
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_AS AS;
    CD3DX12_PIPELINE_STATE_STREAM_MS MS;
    CD3DX12_PIPELINE_STATE_STREAM_PS PS;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC Blend;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
};

SDL_Window* window = nullptr;
IDXGIFactory7* factory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12Device14* device = nullptr;
ID3D12Resource* renderTargets[FRAME_COUNT] = {};
ID3D12CommandAllocator* cmdAlloc[FRAME_COUNT] = {};
ID3D12CommandQueue* cmdQueue = nullptr;

ID3D12RootSignature* rootSignature = nullptr;
ID3D12PipelineState* pipelineState = nullptr;

ID3D12RootSignature* computeRootSignature = nullptr;
ID3D12PipelineState* computePipelineState = nullptr;

ID3D12CommandSignature* commandSignature = nullptr;
ID3D12Resource* argumentBuffer = nullptr;
ID3D12Resource* instanceDataBuffer = nullptr;

ID3D12DescriptorHeap* rtvHeap = nullptr;
ID3D12GraphicsCommandList6* cmdList = nullptr;
UINT rtvDescriptorSize = 0;

ID3D12Resource* constantBuffer = nullptr;
UINT8* pCbvDataBegin = nullptr;
UINT alignedCBSize = 0;

ID3D12Resource* depthStencilBuffer = nullptr;
ID3D12DescriptorHeap* dsvHeap = nullptr;

UINT frameIndex = 0;
HANDLE fenceEvent = 0;
ID3D12Fence* fence = nullptr;
UINT64 frameFenceValues[FRAME_COUNT] = {};
UINT64 fenceValue = 1;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("test", WINDOW_WIDTH, WINDOW_HEIGHT, NULL);

#if defined(_DEBUG)
    ID3D12Debug* debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        debugController->Release();
    }
#endif

    IDXGIAdapter4* adapter = nullptr;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
    adapter->Release();
    adapter = nullptr;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureData = {};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureData, sizeof(featureData));
    if (featureData.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        return SDL_APP_FAILURE;
    }

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

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
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

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

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

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &depthResDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&depthStencilBuffer));
    device->CreateDepthStencilView(depthStencilBuffer, nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc[i]));
    }
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc[0], nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParameters;

    ID3DBlob* signatureBlob = nullptr;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, nullptr);
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    signatureBlob->Release();
    signatureBlob = nullptr;

    D3D12_ROOT_PARAMETER computeRootParams[3] = {};
    computeRootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    computeRootParams[0].Descriptor.ShaderRegister = 0;
    computeRootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    computeRootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    computeRootParams[1].Descriptor.ShaderRegister = 0;
    computeRootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    computeRootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    computeRootParams[2].Descriptor.ShaderRegister = 1;
    computeRootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC computeRootSigDesc = {};
    computeRootSigDesc.NumParameters = 3;
    computeRootSigDesc.pParameters = computeRootParams;

    D3D12SerializeRootSignature(&computeRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, nullptr);
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature));
    signatureBlob->Release();
    signatureBlob = nullptr;

    size_t asSize = 0, msSize = 0, psSize = 0, csSize = 0;
    void* asData = SDL_LoadFile("shaders/dxil/AmplificationShader.dxil", &asSize);
    void* msData = SDL_LoadFile("shaders/dxil/MeshShader.dxil", &msSize);
    void* psData = SDL_LoadFile("shaders/dxil/PixelShader.dxil", &psSize);
    void* csData = SDL_LoadFile("shaders/dxil/ComputeShader.dxil", &csSize);

    MeshShaderPipelineStateStream pipelineStateStream = {};
    pipelineStateStream.pRootSignature = rootSignature;
    pipelineStateStream.AS = CD3DX12_SHADER_BYTECODE(asData, asSize);
    pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(msData, msSize);
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psData, psSize);

    CD3DX12_RASTERIZER_DESC rastDesc(D3D12_DEFAULT);
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    pipelineStateStream.Rasterizer = rastDesc;

    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    pipelineStateStream.Blend = blendDesc;

    CD3DX12_DEPTH_STENCIL_DESC depthDesc(D3D12_DEFAULT);
    pipelineStateStream.DepthStencil = depthDesc;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineStateStream.RTVFormats = rtvFormats;
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(MeshShaderPipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    HRESULT hr = device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pipelineState));
    if (FAILED(hr))
    {
        return SDL_APP_FAILURE;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = computeRootSignature;
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(csData, csSize);
    device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&computePipelineState));

    SDL_free(asData);
    SDL_free(msData);
    SDL_free(psData);
    SDL_free(csData);

    D3D12_INDIRECT_ARGUMENT_DESC indirectArg = {};
    indirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
    cmdSigDesc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
    cmdSigDesc.NumArgumentDescs = 1;
    cmdSigDesc.pArgumentDescs = &indirectArg;

    device->CreateCommandSignature(&cmdSigDesc, nullptr, IID_PPV_ARGS(&commandSignature));

    D3D12_RESOURCE_DESC argBufDesc = {};
    argBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    argBufDesc.Width = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
    argBufDesc.Height = 1;
    argBufDesc.DepthOrArraySize = 1;
    argBufDesc.MipLevels = 1;
    argBufDesc.SampleDesc.Count = 1;
    argBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    argBufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &argBufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&argumentBuffer));

    D3D12_RESOURCE_DESC instBufDesc = argBufDesc;
    instBufDesc.Width = sizeof(XMMATRIX) * INSTANCE_COUNT;

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &instBufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&instanceDataBuffer));

    cmdAlloc[frameIndex]->Reset();
    cmdList->Reset(cmdAlloc[frameIndex], nullptr);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = argumentBuffer;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = instanceDataBuffer;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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

    alignedCBSize = (sizeof(ComputeConstants) + 255) & ~255;
    UINT totalCBSize = alignedCBSize * FRAME_COUNT;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = totalCBSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer));

    D3D12_RANGE readRange = {};
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
    cmdList->Reset(cmdAlloc[frameIndex], nullptr);

    static float timeAcc = 0.0f;
    timeAcc += 0.016f;

    XMMATRIX mView = XMMatrixLookAtLH(XMVectorSet(0.0f, 15.0f, -20.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.1f, 1000.0f);

    ComputeConstants cbData;
    cbData.viewProj = XMMatrixTranspose(mView * mProj);
    cbData.time = timeAcc;
    cbData.totalInstances = currentInstanceCount;

    UINT8* destCB = pCbvDataBegin + (frameIndex * alignedCBSize);
    memcpy(destCB, &cbData, sizeof(cbData));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = constantBuffer->GetGPUVirtualAddress() + (frameIndex * alignedCBSize);

    cmdList->SetComputeRootSignature(computeRootSignature);
    cmdList->SetPipelineState(computePipelineState);
    cmdList->SetComputeRootConstantBufferView(0, cbAddress);
    cmdList->SetComputeRootUnorderedAccessView(1, argumentBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2, instanceDataBuffer->GetGPUVirtualAddress());

    UINT dispatchGroups = (currentInstanceCount + 63) / 64;
    cmdList->Dispatch(dispatchGroups, 1, 1);

    D3D12_RESOURCE_BARRIER preDrawBarriers[2] = {};
    preDrawBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preDrawBarriers[0].Transition.pResource = argumentBuffer;
    preDrawBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    preDrawBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    preDrawBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    preDrawBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preDrawBarriers[1].Transition.pResource = instanceDataBuffer;
    preDrawBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    preDrawBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    preDrawBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(2, preDrawBarriers);

    cmdList->SetPipelineState(pipelineState);
    cmdList->SetGraphicsRootSignature(rootSignature);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)WINDOW_WIDTH, (LONG)WINDOW_HEIGHT };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    D3D12_RESOURCE_BARRIER rtBarrier = {};
    rtBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    rtBarrier.Transition.pResource = renderTargets[frameIndex];
    rtBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    rtBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    rtBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &rtBarrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * rtvDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
    cmdList->SetGraphicsRootShaderResourceView(1, instanceDataBuffer->GetGPUVirtualAddress());

    cmdList->ExecuteIndirect(commandSignature, 1, argumentBuffer, 0, nullptr, 0);

    D3D12_RESOURCE_BARRIER postDrawBarriers[2] = {};
    postDrawBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postDrawBarriers[0].Transition.pResource = argumentBuffer;
    postDrawBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    postDrawBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    postDrawBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    postDrawBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postDrawBarriers[1].Transition.pResource = instanceDataBuffer;
    postDrawBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    postDrawBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    postDrawBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(2, postDrawBarriers);

    D3D12_RESOURCE_BARRIER rtPresentBarrier = {};
    rtPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    rtPresentBarrier.Transition.pResource = renderTargets[frameIndex];
    rtPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    rtPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    rtPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &rtPresentBarrier);

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
    case SDL_EVENT_KEY_DOWN:
        switch (event->key.scancode)
        {
        case SDL_SCANCODE_UP:
            if (currentInstanceCount < INSTANCE_COUNT)
            {
                currentInstanceCount += 1;
            }
            break;
        case SDL_SCANCODE_DOWN:
            if (currentInstanceCount > 1)
            {
                currentInstanceCount -= 1;
            }
            break;
        }
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    const UINT64 fenceToWaitFor = fenceValue;
    cmdQueue->Signal(fence, fenceToWaitFor);
    fenceValue++;

    fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);

    CloseHandle(fenceEvent);
    fenceEvent = nullptr;

    commandSignature->Release();
    commandSignature = nullptr;

    argumentBuffer->Release();
    argumentBuffer = nullptr;

    instanceDataBuffer->Release();
    instanceDataBuffer = nullptr;

    computePipelineState->Release();
    computePipelineState = nullptr;

    computeRootSignature->Release();
    computeRootSignature = nullptr;

    fence->Release();
    fence = nullptr;

    constantBuffer->Unmap(0, nullptr);
    constantBuffer->Release();
    constantBuffer = nullptr;

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

        renderTargets[i]->Release();
        renderTargets[i] = nullptr;
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