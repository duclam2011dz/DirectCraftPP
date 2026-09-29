cbuffer Camera : register(b0) {
    float4x4 viewProjection;
    float3 cameraPosition;
    float padding;
};

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
};

PixelInput VSMain(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.normal = input.normal;
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET {
    float3 lightDirection = normalize(float3(-0.45, 0.9, -0.35));
    float diffuse = saturate(dot(normalize(input.normal), lightDirection));
    float lighting = 0.32 + diffuse * 0.68;
    return float4(input.color.rgb * lighting, input.color.a);
}
