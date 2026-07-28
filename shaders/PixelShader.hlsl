struct PSInput
{
    float4 position : SV_POSITION;
};


float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
