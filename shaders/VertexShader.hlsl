struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    
    output.position = float4(input.position.x * 0.75, input.position.y, input.position.z, 1.0f);
    
    return output;
}