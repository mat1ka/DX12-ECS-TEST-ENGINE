struct InstanceData
{
    matrix worldMatrix;
};

struct DrawIndexedArguments
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

cbuffer GlobalConstants : register(b0)
{
    matrix viewProj;
    float time;
    uint totalInstances;
};

RWStructuredBuffer<DrawIndexedArguments> g_CommandArgs : register(u0);
RWStructuredBuffer<InstanceData> g_ObjectTransforms : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint instanceID = dispatchThreadID.x;
    if (instanceID >= totalInstances)
        return;

    if (instanceID == 0)
    {
        g_CommandArgs[0].IndexCountPerInstance = 36;
        g_CommandArgs[0].InstanceCount = totalInstances;
        g_CommandArgs[0].StartIndexLocation = 0;
        g_CommandArgs[0].BaseVertexLocation = 0;
        g_CommandArgs[0].StartInstanceLocation = 0;
    }

    float posX = (float) instanceID * 2.0f;
    float posY = 0.0f;
    float posZ = 0.0f;

    float angle = time + (float) instanceID * 0.1f;
    float cosA = cos(angle);
    float sinA = sin(angle);

    matrix world = matrix
    (
         cosA, 0.0f, sinA, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
        -sinA, 0.0f, cosA, 0.0f,
         posX, posY, posZ, 1.0f
    );

    g_ObjectTransforms[instanceID].worldMatrix = world;
}