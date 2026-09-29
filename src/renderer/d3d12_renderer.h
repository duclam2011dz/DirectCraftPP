#pragma once
#include "voxel/voxel.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <filesystem>
#include <string>

namespace directcraft::renderer {
class D3D12Renderer {
public:
    D3D12Renderer(HWND window, int width, int height, bool renderTest);
    ~D3D12Renderer();
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;
    void setMesh(const voxel::Mesh& mesh);
    void setWireframe(bool enabled);
    bool wireframe() const { return wireframe_; }
    std::string adapterName() const;
    bool usingWarp() const { return usingWarp_; }
    std::string featureLevel() const { return "11_0"; }
    bool pipelineReady() const { return pipelineState_ != nullptr && rootSignature_ != nullptr; }
    bool resourcesReady() const { return vertexBuffer_ != nullptr && indexBuffer_ != nullptr && depthBuffer_ != nullptr; }
    bool gpuValidationEnabled() const { return gpuValidationEnabled_; }
    bool dredEnabled() const { return dredEnabled_; }
    HRESULT deviceRemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : E_FAIL; }
    UINT64 vertexBytes() const { return vertexBytes_; }
    UINT64 indexBytes() const { return indexBytes_; }
    void render(const DirectX::XMMATRIX& viewProjection, const std::filesystem::path& screenshotPath = {});
private:
    struct CameraConstants { DirectX::XMFLOAT4X4 viewProjection; DirectX::XMFLOAT3 cameraPosition; float padding{}; };
    void createDevice(bool renderTest); void createSwapchain(HWND window); void createTargets(); void createPipeline();
    void uploadMesh(const voxel::Mesh& mesh); void waitForGpu(); void captureBackBuffer(const std::filesystem::path& path);
    void writePng(const std::filesystem::path& path, const void* pixels, UINT rowPitch);
    static Microsoft::WRL::ComPtr<ID3DBlob> compileShader(const std::filesystem::path& path, const char* entry, const char* profile);
    static constexpr UINT FrameCount = 2;
    int width_{}; int height_{}; bool renderTest_{}; bool wireframe_{}; bool usingWarp_{}; bool gpuValidationEnabled_{}; bool dredEnabled_{}; UINT frameIndex_{};
    DXGI_ADAPTER_DESC1 adapterDescription_{}; UINT64 vertexBytes_{}; UINT64 indexBytes_{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_; Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_; Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_; Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_; Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_; Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_; Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_{}; UINT64 fenceValue_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_; Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_; Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets_[FrameCount]; Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_; UINT rtvDescriptorSize_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_; Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_; Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_; CameraConstants* mappedConstants_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_; Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_; D3D12_VERTEX_BUFFER_VIEW vertexView_{}; D3D12_INDEX_BUFFER_VIEW indexView_{}; UINT indexCount_{};
};
} // namespace directcraft::renderer
