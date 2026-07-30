struct DrawIndexedArguments
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

RWStructuredBuffer<DrawIndexedArguments> IndirectCommands : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    IndirectCommands[0].IndexCountPerInstance = 36; 
    IndirectCommands[0].InstanceCount = 64; 
    IndirectCommands[0].StartIndexLocation = 0;
    IndirectCommands[0].BaseVertexLocation = 0;
    IndirectCommands[0].StartInstanceLocation = 0;
}