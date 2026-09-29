#include <DirectXMath.h>

#include <cmath>
#include <iostream>

using namespace DirectX;

int main() {
    const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(8.0f, 16.0f, -24.0f, 1.0f), XMVectorSet(8.0f, 8.0f, 8.0f, 1.0f), XMVectorSet(0, 1, 0, 0));
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f / 720.0f, 0.1f, 200.0f);
    const XMMATRIX logical = view * projection; const XMMATRIX uploaded = XMMatrixTranspose(logical);
    for (const XMVECTOR point : {XMVectorSet(8, 8, 8, 1), XMVectorSet(0, 10, 0, 1), XMVectorSet(15, 0, 15, 1)}) {
        XMFLOAT4 clip{};
        XMStoreFloat4(&clip, XMVector4Transform(point, logical));
        std::cout << clip.x << "," << clip.y << "," << clip.z << "," << clip.w << "\n";
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) || clip.w <= 0.001f) return 1;
        const float x = clip.x / clip.w; const float y = clip.y / clip.w; const float z = clip.z / clip.w;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > 100.0f || std::abs(y) > 100.0f || z < -100.0f || z > 100.0f) return 2;
    }
    std::cout << "Matrix convention regression passed\n";
    return 0;
}
