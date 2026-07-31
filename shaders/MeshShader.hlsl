// HLSL 6.6 (Shader Model 6.6)

struct InstanceData
{
    matrix worldMatrix;
};

cbuffer GlobalConstants : register(b0)
{
    matrix viewProj;
    float time;
    uint totalInstances;
    uint cmdArgsUAVIndex;
    uint objectTransformsIndex; // Тот же индекс (или SRV-индекс) буфера инстансов
};

struct Payload
{
    uint InstanceID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

[numthreads(32, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    in payload Payload payload,
    in uint gtid : SV_GroupThreadID,
    out vertices PSInput outs[8],
    out indices uint3 triangles[12])
{
    SetMeshOutputCounts(8, 12);

    const float3 positions[8] =
    {
        float3(-0.5f, -0.5f, -0.5f),
        float3(-0.5f, 0.5f, -0.5f),
        float3(0.5f, 0.5f, -0.5f),
        float3(0.5f, -0.5f, -0.5f),
        float3(-0.5f, -0.5f, 0.5f),
        float3(-0.5f, 0.5f, 0.5f),
        float3(0.5f, 0.5f, 0.5f),
        float3(0.5f, -0.5f, 0.5f)
    };

    const float4 colors[8] =
    {
        float4(0.0f, 0.0f, 0.0f, 1.0f),
        float4(0.0f, 1.0f, 0.0f, 1.0f),
        float4(1.0f, 1.0f, 0.0f, 1.0f),
        float4(1.0f, 0.0f, 0.0f, 1.0f),
        float4(0.0f, 0.0f, 1.0f, 1.0f),
        float4(0.0f, 1.0f, 1.0f, 1.0f),
        float4(1.0f, 1.0f, 1.0f, 1.0f),
        float4(1.0f, 0.0f, 1.0f, 1.0f)
    };

    const uint3 indicesList[12] =
    {
        uint3(0, 1, 2), uint3(0, 2, 3),
        uint3(4, 6, 5), uint3(4, 7, 6),
        uint3(4, 5, 1), uint3(4, 1, 0),
        uint3(3, 2, 6), uint3(3, 6, 7),
        uint3(1, 5, 6), uint3(1, 6, 2),
        uint3(4, 0, 3), uint3(4, 3, 7)
    };

    if (gtid < 8)
    {
        // Достаем SRV-буфер из Bindless Heap
        StructuredBuffer<InstanceData> g_ObjectTransforms = ResourceDescriptorHeap[objectTransformsIndex];

        matrix world = g_ObjectTransforms[payload.InstanceID].worldMatrix;
        matrix wvp = mul(world, viewProj);

        outs[gtid].position = mul(float4(positions[gtid], 1.0f), wvp);
        outs[gtid].color = colors[gtid];
    }

    if (gtid < 12)
    {
        triangles[gtid] = indicesList[gtid];
    }
}