cbuffer ConstantBuffer : register(b0)
{
    matrix mvp;
};

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

PSInput VSMain(VSInput input)
{
    PSInput output;
    
    output.position = mul(float4(input.position.x, input.position.y, input.position.z, 1.0f), mvp);
    output.color = input.color;
    
    return output;
}