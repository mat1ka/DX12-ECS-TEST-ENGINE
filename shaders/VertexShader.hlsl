struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct InstanceData
{
    matrix worldMatrix;
};

cbuffer GlobalConstants : register(b0)
{
    matrix viewProj;
    float time;
    uint totalInstances;
};

StructuredBuffer<InstanceData> g_ObjectTransforms : register(t0);

PSInput VSMain(VSInput input, uint instanceID : SV_InstanceID)
{
    PSInput output;
    
    matrix world = g_ObjectTransforms[instanceID].worldMatrix;
    matrix mvp = mul(world, viewProj);
    
    output.position = mul(float4(input.position, 1.0f), mvp);
    output.color = input.color;
    return output;
}