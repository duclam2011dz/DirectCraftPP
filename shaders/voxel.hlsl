cbuffer Camera : register(b0) {
    float4x4 viewProjection;
    float3 cameraPosition;
    float padding;
};
Texture2D blockAtlas : register(t0);
SamplerState blockSampler : register(s0);

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float ao : AO0;
    uint material : MATERIAL0;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float ao : AO0;
    float2 uv : TEXCOORD0;
    uint material : MATERIAL0;
};

PixelInput VSMain(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.normal = input.normal;
    output.color = input.color;
    output.ao = input.ao;
    output.uv = input.uv;
    output.material = input.material;
    return output;
}

struct PackedVertexInput {
    uint packedPosition : PACKED_POSITION;
    uint packedAttributes : PACKED_ATTRIBUTES;
};

PixelInput VSMainPacked(PackedVertexInput input) {
    PixelInput output;
    uint px = input.packedPosition & 1023;
    uint py = (input.packedPosition >> 10) & 1023;
    uint pz = (input.packedPosition >> 20) & 1023;
    float3 position = float3((int)px - 512, (int)py - 512, (int)pz - 512);
    uint normalCode = input.packedAttributes & 7;
    output.normal = float3(0, 0, -1);
    if (normalCode == 1) output.normal = float3(1, 0, 0);
    else if (normalCode == 2) output.normal = float3(-1, 0, 0);
    else if (normalCode == 3) output.normal = float3(0, 1, 0);
    else if (normalCode == 4) output.normal = float3(0, -1, 0);
    else if (normalCode == 5) output.normal = float3(0, 0, 1);
    uint aoCode = (input.packedAttributes >> 3) & 3;
    output.ao = 1.0 - (float)aoCode / 3.0;
    uint material = (input.packedAttributes >> 5) & 7;
    float3 colors[5] = {float3(0.78, 0.86, 0.38), float3(0.68, 0.68, 0.68), float3(0.56, 0.56, 0.56), float3(0.68, 0.68, 0.68), float3(0.24, 0.55, 0.82)};
    output.color = float4(colors[min(material, 4)], 1.0);
    uint uvCode = (input.packedAttributes >> 8) & 3;
    output.uv = float2((uvCode & 1) ? 1.0 : 0.0, (uvCode & 2) ? 1.0 : 0.0);
    output.material = material;
    output.position = mul(float4(position, 1.0), viewProjection);
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET {
    if (input.color.a < 0.0) return float4(0.0, 0.0, 0.0, 1.0);
    float3 lightDirection = normalize(float3(-0.45, 0.9, -0.35));
    float diffuse = saturate(dot(normalize(input.normal), lightDirection));
    float lighting = 0.32 + diffuse * 0.68;
    float2 tileSize = float2(0.25, 0.5);
    float2 atlasUv = float2((input.material % 4) * tileSize.x + input.uv.x * tileSize.x, (input.material / 4) * tileSize.y + input.uv.y * tileSize.y);
    float3 textureColor = blockAtlas.Sample(blockSampler, atlasUv).rgb;
    return float4(textureColor * lighting * input.ao, input.color.a);
}
