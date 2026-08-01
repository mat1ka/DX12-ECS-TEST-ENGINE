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

const UINT INSTANCE_COUNT = 7;
UINT currentInstanceCount = 1;

enum DescriptorIndices
{
    DESC_CMD_ARGS_UAV = 0,
    DESC_INSTANCE_DATA_UAV_SRV = 1,
    DESC_COUNT = 2
};

struct alignas(16) GlobalConstants
{
    XMMATRIX viewProj;
    float time;
    UINT totalInstances;
    UINT cmdArgsUAVIndex;
    UINT objectTransformsIndex;
};

template <typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectType>
struct alignas(void*) PipelineStateStreamSubobject
{
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = SubobjectType;
    T Data = {};
};

struct alignas(void*) MeshShaderPipelineStateStream
{
    PipelineStateStreamSubobject<ID3D12RootSignature*, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> pRootSignature;
    PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> AS;
    PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> MS;
    PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> PS;
    PipelineStateStreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> Rasterizer;
    PipelineStateStreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> Blend;
    PipelineStateStreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> DepthStencil;
    PipelineStateStreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> RTVFormats;
    PipelineStateStreamSubobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> DSVFormat;
};

SDL_Window* window = nullptr;
IDXGIFactory7* factory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12Device14* device = nullptr;
ID3D12Resource* renderTargets[FRAME_COUNT] = {};
ID3D12CommandAllocator* cmdAlloc[FRAME_COUNT] = {};
ID3D12CommandQueue* cmdQueue = nullptr;

ID3D12RootSignature* commonRootSignature = nullptr;
ID3D12PipelineState* pipelineState = nullptr;
ID3D12PipelineState* computePipelineState = nullptr;

ID3D12CommandSignature* commandSignature = nullptr;
ID3D12Resource* argumentBuffer = nullptr;
ID3D12Resource* instanceDataBuffer = nullptr;

ID3D12DescriptorHeap* rtvHeap = nullptr;
ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
ID3D12GraphicsCommandList7* cmdList = nullptr;
UINT rtvDescriptorSize = 0;
UINT cbvSrvUavDescriptorSize = 0;

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
    window = SDL_CreateWindow("test", WINDOW_WIDTH, WINDOW_HEIGHT, 0);

#if defined(_DEBUG)
    ID3D12Debug* debugController = nullptr;
    D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
    debugController->EnableDebugLayer();
    debugController->Release();
#endif

    IDXGIAdapter4* adapter = nullptr;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
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

    D3D12_DESCRIPTOR_HEAP_DESC bindlessHeapDesc = {};
    bindlessHeapDesc.NumDescriptors = DESC_COUNT;
    bindlessHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    bindlessHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&bindlessHeapDesc, IID_PPV_ARGS(&cbvSrvUavHeap));

    cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeapProps.CreationNodeMask = 1;
    defaultHeapProps.VisibleNodeMask = 1;

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

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    ID3DBlob* signatureBlob = nullptr;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, nullptr);
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&commonRootSignature));
    signatureBlob->Release();
    signatureBlob = nullptr;

    size_t asSize = 0, msSize = 0, psSize = 0, csSize = 0;
    void* asData = SDL_LoadFile("shaders/dxil/AmplificationShader.dxil", &asSize);
    void* msData = SDL_LoadFile("shaders/dxil/MeshShader.dxil", &msSize);
    void* psData = SDL_LoadFile("shaders/dxil/PixelShader.dxil", &psSize);
    void* csData = SDL_LoadFile("shaders/dxil/ComputeShader.dxil", &csSize);

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rastDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rastDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rastDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
    }

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    depthDesc.FrontFace = defaultStencilOp;
    depthDesc.BackFace = defaultStencilOp;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    MeshShaderPipelineStateStream pipelineStateStream = {};
    pipelineStateStream.pRootSignature.Data = commonRootSignature;
    pipelineStateStream.AS.Data = { asData, asSize };
    pipelineStateStream.MS.Data = { msData, msSize };
    pipelineStateStream.PS.Data = { psData, psSize };
    pipelineStateStream.Rasterizer.Data = rastDesc;
    pipelineStateStream.Blend.Data = blendDesc;
    pipelineStateStream.DepthStencil.Data = depthDesc;
    pipelineStateStream.RTVFormats.Data = rtvFormats;
    pipelineStateStream.DSVFormat.Data = DXGI_FORMAT_D32_FLOAT;

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(MeshShaderPipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pipelineState));

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = commonRootSignature;
    computePsoDesc.CS = { csData, csSize };
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
    argBufDesc.Format = DXGI_FORMAT_UNKNOWN;
    argBufDesc.SampleDesc.Count = 1;
    argBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    argBufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &argBufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&argumentBuffer));

    D3D12_RESOURCE_DESC instBufDesc = argBufDesc;
    instBufDesc.Width = sizeof(XMMATRIX) * INSTANCE_COUNT;

    device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &instBufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&instanceDataBuffer));

    D3D12_CPU_DESCRIPTOR_HANDLE heapCpuHandle = cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavArgDesc = {};
    uavArgDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavArgDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavArgDesc.Buffer.NumElements = 1;
    uavArgDesc.Buffer.StructureByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
    device->CreateUnorderedAccessView(argumentBuffer, nullptr, &uavArgDesc, heapCpuHandle);

    heapCpuHandle.ptr += cbvSrvUavDescriptorSize;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavInstDesc = {};
    uavInstDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavInstDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavInstDesc.Buffer.NumElements = INSTANCE_COUNT;
    uavInstDesc.Buffer.StructureByteStride = sizeof(XMMATRIX);
    device->CreateUnorderedAccessView(instanceDataBuffer, nullptr, &uavInstDesc, heapCpuHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvInstDesc = {};
    srvInstDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvInstDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvInstDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvInstDesc.Buffer.NumElements = INSTANCE_COUNT;
    srvInstDesc.Buffer.StructureByteStride = sizeof(XMMATRIX);
    device->CreateShaderResourceView(instanceDataBuffer, &srvInstDesc, heapCpuHandle);

    cmdAlloc[frameIndex]->Reset();
    cmdList->Reset(cmdAlloc[frameIndex], nullptr);

    D3D12_BUFFER_BARRIER initBarriers[2] = {};
    initBarriers[0].pResource = argumentBuffer;
    initBarriers[0].SyncBefore = D3D12_BARRIER_SYNC_NONE;
    initBarriers[0].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    initBarriers[0].AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    initBarriers[0].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    initBarriers[0].Size = UINT64_MAX;

    initBarriers[1].pResource = instanceDataBuffer;
    initBarriers[1].SyncBefore = D3D12_BARRIER_SYNC_NONE;
    initBarriers[1].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    initBarriers[1].AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    initBarriers[1].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    initBarriers[1].Size = UINT64_MAX;

    D3D12_BARRIER_GROUP initBarrierGroup = {};
    initBarrierGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
    initBarrierGroup.NumBarriers = 2;
    initBarrierGroup.pBufferBarriers = initBarriers;

    cmdList->Barrier(1, &initBarrierGroup);
    cmdList->Close();

    ID3D12CommandList* ppCmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, ppCmdLists);

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceValue = 1;
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    const UINT64 fenceToWaitFor = fenceValue;
    cmdQueue->Signal(fence, fenceToWaitFor);
    fenceValue++;

    fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
    if (fenceEvent != nullptr)
    {
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    alignedCBSize = (sizeof(GlobalConstants) + 255) & ~255;
    UINT totalCBSize = alignedCBSize * FRAME_COUNT;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

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

    D3D12_RANGE readRange = {};
    constantBuffer->Map(0, &readRange, (void**)(&pCbvDataBegin));

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    fence->SetEventOnCompletion(frameFenceValues[frameIndex], fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);

    cmdAlloc[frameIndex]->Reset();
    cmdList->Reset(cmdAlloc[frameIndex], nullptr);

    static float timeAcc = 0.0f;
    timeAcc += 0.016f;

    XMMATRIX mView = XMMatrixLookAtLH(XMVectorSet(0.0f, 15.0f, -20.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.1f, 1000.0f);

    GlobalConstants cbData;
    cbData.viewProj = XMMatrixTranspose(mView * mProj);
    cbData.time = timeAcc;
    cbData.totalInstances = currentInstanceCount;
    cbData.cmdArgsUAVIndex = DESC_CMD_ARGS_UAV;
    cbData.objectTransformsIndex = DESC_INSTANCE_DATA_UAV_SRV;

    UINT8* destCB = pCbvDataBegin + (frameIndex * alignedCBSize);
    memcpy(destCB, &cbData, sizeof(cbData));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = constantBuffer->GetGPUVirtualAddress() + (frameIndex * alignedCBSize);

    ID3D12DescriptorHeap* descriptorHeaps[] = { cbvSrvUavHeap };
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);

    cmdList->SetComputeRootSignature(commonRootSignature);
    cmdList->SetPipelineState(computePipelineState);
    cmdList->SetComputeRootConstantBufferView(0, cbAddress);

    UINT dispatchGroups = (currentInstanceCount + 63) / 64;
    cmdList->Dispatch(dispatchGroups, 1, 1);

    D3D12_BUFFER_BARRIER preDrawBarriers[2] = {};
    preDrawBarriers[0].pResource = argumentBuffer;
    preDrawBarriers[0].SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    preDrawBarriers[0].SyncAfter = D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
    preDrawBarriers[0].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    preDrawBarriers[0].AccessAfter = D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
    preDrawBarriers[0].Size = UINT64_MAX;

    preDrawBarriers[1].pResource = instanceDataBuffer;
    preDrawBarriers[1].SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    preDrawBarriers[1].SyncAfter = D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
    preDrawBarriers[1].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    preDrawBarriers[1].AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    preDrawBarriers[1].Size = UINT64_MAX;

    D3D12_BARRIER_GROUP preDrawGroup = {};
    preDrawGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
    preDrawGroup.NumBarriers = 2;
    preDrawGroup.pBufferBarriers = preDrawBarriers;

    cmdList->Barrier(1, &preDrawGroup);

    cmdList->SetPipelineState(pipelineState);
    cmdList->SetGraphicsRootSignature(commonRootSignature);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)WINDOW_WIDTH, (LONG)WINDOW_HEIGHT };

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    D3D12_TEXTURE_BARRIER rtBarrier = {};
    rtBarrier.pResource = renderTargets[frameIndex];
    rtBarrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
    rtBarrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
    rtBarrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    rtBarrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    rtBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
    rtBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    rtBarrier.Subresources.IndexOrFirstMipLevel = 0xffffffff;

    D3D12_BARRIER_GROUP rtGroup = {};
    rtGroup.Type = D3D12_BARRIER_TYPE_TEXTURE;
    rtGroup.NumBarriers = 1;
    rtGroup.pTextureBarriers = &rtBarrier;

    cmdList->Barrier(1, &rtGroup);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * rtvDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
    cmdList->ExecuteIndirect(commandSignature, 1, argumentBuffer, 0, nullptr, 0);

    D3D12_BUFFER_BARRIER postDrawBarriers[2] = {};
    postDrawBarriers[0].pResource = argumentBuffer;
    postDrawBarriers[0].SyncBefore = D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
    postDrawBarriers[0].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    postDrawBarriers[0].AccessBefore = D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
    postDrawBarriers[0].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    postDrawBarriers[0].Size = UINT64_MAX;

    postDrawBarriers[1].pResource = instanceDataBuffer;
    postDrawBarriers[1].SyncBefore = D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
    postDrawBarriers[1].SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    postDrawBarriers[1].AccessBefore = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    postDrawBarriers[1].AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    postDrawBarriers[1].Size = UINT64_MAX;

    D3D12_BARRIER_GROUP postDrawGroup = {};
    postDrawGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
    postDrawGroup.NumBarriers = 2;
    postDrawGroup.pBufferBarriers = postDrawBarriers;

    cmdList->Barrier(1, &postDrawGroup);

    D3D12_TEXTURE_BARRIER rtPresentBarrier = {};
    rtPresentBarrier.pResource = renderTargets[frameIndex];
    rtPresentBarrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    rtPresentBarrier.SyncAfter = D3D12_BARRIER_SYNC_NONE;
    rtPresentBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    rtPresentBarrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    rtPresentBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    rtPresentBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
    rtPresentBarrier.Subresources.IndexOrFirstMipLevel = 0xffffffff;

    D3D12_BARRIER_GROUP rtPresentGroup = {};
    rtPresentGroup.Type = D3D12_BARRIER_TYPE_TEXTURE;
    rtPresentGroup.NumBarriers = 1;
    rtPresentGroup.pTextureBarriers = &rtPresentBarrier;

    cmdList->Barrier(1, &rtPresentGroup);

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
        break;
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

    cbvSrvUavHeap->Release();
    cbvSrvUavHeap = nullptr;

    computePipelineState->Release();
    computePipelineState = nullptr;

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

    commonRootSignature->Release();
    commonRootSignature = nullptr;

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