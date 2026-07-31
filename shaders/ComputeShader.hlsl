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
    uint cmdArgsUAVIndex; 
    uint objectTransformsUAVIndex; 
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint instanceID = dispatchThreadID.x;
    if (instanceID >= totalInstances)
        return;

    RWStructuredBuffer<DispatchMeshArguments> commandArgs = ResourceDescriptorHeap[cmdArgsUAVIndex];
    RWStructuredBuffer<InstanceData> objectTransforms = ResourceDescriptorHeap[objectTransformsUAVIndex];

    if (instanceID == 0)
    {
        commandArgs[0].ThreadGroupCountX = totalInstances;
        commandArgs[0].ThreadGroupCountY = 1;
        commandArgs[0].ThreadGroupCountZ = 1;
    }

    float posX = (float)instanceID * 2.0f;
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

    objectTransforms[instanceID].worldMatrix = world;
}