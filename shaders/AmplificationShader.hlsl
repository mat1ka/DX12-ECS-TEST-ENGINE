struct Payload
{

    uint InstanceID;
};
[numthreads(1, 1, 1)]
void ASMain(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{

    Payload payload;

    payload.InstanceID = gid.x;
    DispatchMesh(1, 1, 1, payload);
}

