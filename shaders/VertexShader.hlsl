struct Vertex
{
    float3 position;
    float4 color;
};

struct InstanceData
{
    matrix mvp;
};

StructuredBuffer<Vertex> VertexBuffer : register(t0);
StructuredBuffer<InstanceData> InstanceBuffer : register(t1);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    PSInput result;
    
    Vertex v = VertexBuffer[vertexID];
    InstanceData inst = InstanceBuffer[instanceID];
    
    result.position = mul(float4(v.position, 1.0f), inst.mvp);
    result.color = v.color;
    
    return result;
}