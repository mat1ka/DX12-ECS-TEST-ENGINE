// HLSL 6.6 (Shader Model 6.6)

struct InstanceData
{
    matrix worldMatrix;
};

struct DispatchMeshArguments
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

cbuffer GlobalConstants : register(b0)
{
    matrix viewProj;
    float time;
    uint totalInstances;
    // Bindless-индексы для записи на GPU
    uint cmdArgsUAVIndex; // Индекс RWStructuredBuffer<DispatchMeshArguments> в DescriptorHeap
    uint objectTransformsUAVIndex; // Индекс RWStructuredBuffer<InstanceData> в DescriptorHeap
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint instanceID = dispatchThreadID.x;
    if (instanceID >= totalInstances)
        return;

    // Достаем RW-буферы НАПРЯМУЮ из ResourceDescriptorHeap по их индексам
    RWStructuredBuffer<DispatchMeshArguments> g_CommandArgs = ResourceDescriptorHeap[cmdArgsUAVIndex];
    RWStructuredBuffer<InstanceData> g_ObjectTransforms = ResourceDescriptorHeap[objectTransformsUAVIndex];

    if (instanceID == 0)
    {
        g_CommandArgs[0].ThreadGroupCountX = totalInstances;
        g_CommandArgs[0].ThreadGroupCountY = 1;
        g_CommandArgs[0].ThreadGroupCountZ = 1;
    }

    float posX = (float) instanceID * 2.0f;
    float posY = 0.0f;
    float posZ = 0.0f;

    float angle = time;
    float cosA = cos(angle);
    float sinA = sin(angle);

    matrix world = matrix(
         cosA, 0.0f, sinA, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
        -sinA, 0.0f, cosA, 0.0f,
         posX, posY, posZ, 1.0f
    );

    g_ObjectTransforms[instanceID].worldMatrix = world;
}