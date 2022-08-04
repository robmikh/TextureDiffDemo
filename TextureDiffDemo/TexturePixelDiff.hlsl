struct DiffInfo
{
    uint NumDifferingColorPixels;
    uint NumDifferingAlphaPixels;
    uint Width;
    uint Height;
};

RWStructuredBuffer<DiffInfo> diffBuffer : register(u0);
RWTexture2D<unorm float4> colorDiffTexture : register(u1);
RWTexture2D<unorm float4> alphaDiffTexture : register(u2);
Texture2D<unorm float4> inputTexture1 : register(t0);
Texture2D<unorm float4> inputTexture2 : register(t1);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 position = DTid.xy;

    if (position.x < diffBuffer[0].Width && position.y < diffBuffer[0].Height)
    {
        float4 pixel1 = inputTexture1[position];
        float4 pixel2 = inputTexture2[position];

        colorDiffTexture[position] = float4(abs(pixel2.x - pixel1.x), abs(pixel2.y - pixel1.y), abs(pixel2.z - pixel1.z), 1.0f);
        float alphaDiff = abs(pixel2.w - pixel1.w);
        alphaDiffTexture[position] = float4(alphaDiff, alphaDiff, alphaDiff, 1.0f);

        if ((pixel1.x != pixel2.x) || (pixel1.y != pixel2.y) || (pixel1.z != pixel2.z))
        {
            uint value = 0;
            InterlockedAdd(diffBuffer[0].NumDifferingColorPixels, 1, value);
        }

        if (pixel1.w != pixel2.w)
        {
            uint value = 0;
            InterlockedAdd(diffBuffer[0].NumDifferingAlphaPixels, 1, value);
        }
    }
}