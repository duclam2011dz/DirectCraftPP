#include "renderer/d3d12_renderer.h"

#include <d3dcompiler.h>
#include <cstring>
#include <windows.h>
#include <fstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace directcraft::renderer {
namespace {
void check(HRESULT result, const char* message) { if (FAILED(result)) throw std::runtime_error(message); }
D3D12_HEAP_PROPERTIES& uploadHeap() { static D3D12_HEAP_PROPERTIES properties{}; properties.Type = D3D12_HEAP_TYPE_UPLOAD; return properties; }
D3D12_HEAP_PROPERTIES& readbackHeap() { static D3D12_HEAP_PROPERTIES properties{}; properties.Type = D3D12_HEAP_TYPE_READBACK; return properties; }
D3D12_RESOURCE_DESC& bufferDesc(UINT64 size) { static D3D12_RESOURCE_DESC desc{}; desc = {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return desc; }
}

D3D12Renderer::D3D12Renderer(HWND window, int width, int height, bool renderTest) : width_(width), height_(height), renderTest_(renderTest) {
    createDevice(renderTest); createSwapchain(window); createTargets(); createPipeline();
}
D3D12Renderer::~D3D12Renderer() {
    if (device_) waitForGpu();
    if (mappedConstants_) constantBuffer_->Unmap(0, nullptr);
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

void D3D12Renderer::createDevice(bool renderTest) {
    UINT flags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    check(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_)), "Could not create DXGI factory.");
    if (renderTest) {
        check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)), "Could not create WARP adapter.");
    } else {
        for (UINT index = 0; factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++index) {
            DXGI_ADAPTER_DESC1 description{}; adapter_->GetDesc1(&description);
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && SUCCEEDED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) break;
            adapter_.Reset();
        }
        if (!adapter_) check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)), "Could not find a D3D12 adapter.");
    }
    check(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "DirectX 12 device creation failed.");
    D3D12_COMMAND_QUEUE_DESC queueDescription{}; queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&commandQueue_)), "Could not create command queue.");
    check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_)), "Could not create command allocator.");
    check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_)), "Could not create command list.");
    check(commandList_->Close(), "Could not close initial command list.");
    check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Could not create fence.");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); if (!fenceEvent_) throw std::runtime_error("Could not create fence event.");
}

void D3D12Renderer::createSwapchain(HWND window) {
    DXGI_SWAP_CHAIN_DESC1 description{}; description.BufferCount = FrameCount; description.Width = width_; description.Height = height_; description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; description.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> swapchain;
    check(factory_->CreateSwapChainForHwnd(commandQueue_.Get(), window, &description, nullptr, nullptr, &swapchain), "Could not create swapchain.");
    check(swapchain.As(&swapchain_), "Could not query swapchain interface.");
    frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
}

void D3D12Renderer::createTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{}; rtvDescription.NumDescriptors = FrameCount; rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(device_->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&rtvHeap_)), "Could not create RTV heap.");
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < FrameCount; ++index) { check(swapchain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index])), "Could not get swapchain buffer."); device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtv); rtv.ptr += rtvDescriptorSize_; }
    D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{}; dsvDescription.NumDescriptors = 1; dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    check(device_->CreateDescriptorHeap(&dsvDescription, IID_PPV_ARGS(&dsvHeap_)), "Could not create DSV heap.");
    D3D12_RESOURCE_DESC depthDescription{}; depthDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; depthDescription.Width = width_; depthDescription.Height = height_; depthDescription.DepthOrArraySize = 1; depthDescription.MipLevels = 1; depthDescription.Format = DXGI_FORMAT_D32_FLOAT; depthDescription.SampleDesc.Count = 1; depthDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clearValue{}; clearValue.Format = DXGI_FORMAT_D32_FLOAT; clearValue.DepthStencil.Depth = 1.0f;
    auto defaultHeap = D3D12_HEAP_PROPERTIES{}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    check(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDescription, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthBuffer_)), "Could not create depth buffer.");
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

ComPtr<ID3DBlob> D3D12Renderer::compileShader(const std::filesystem::path& path, const char* entry, const char* profile) {
    ComPtr<ID3DBlob> shader; ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors);
    if (FAILED(result)) { if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer())); throw std::runtime_error("HLSL shader compilation failed."); }
    return shader;
}

void D3D12Renderer::createPipeline() {
    D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameter.Descriptor.ShaderRegister = 0; parameter.Descriptor.RegisterSpace = 0; parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{}; rootDescription.NumParameters = 1; rootDescription.pParameters = &parameter; rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized; ComPtr<ID3DBlob> errors; check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Could not serialize root signature.");
    check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), "Could not create root signature.");
    const std::filesystem::path shaderPath = std::filesystem::path(DIRECTCRAFT_SHADER_DIR) / "voxel.hlsl";
    const auto vertexShader = compileShader(shaderPath, "VSMain", "vs_5_0"); const auto pixelShader = compileShader(shaderPath, "PSMain", "ps_5_0");
    D3D12_INPUT_ELEMENT_DESC input[] = {{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_RASTERIZER_DESC rasterizer{}; rasterizer.FillMode = D3D12_FILL_MODE_SOLID; rasterizer.CullMode = D3D12_CULL_MODE_BACK; rasterizer.DepthClipEnable = TRUE; D3D12_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; D3D12_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS; D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature = rootSignature_.Get(); pipeline.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()}; pipeline.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()}; pipeline.InputLayout = {input, _countof(input)}; pipeline.RasterizerState = rasterizer; pipeline.BlendState = blend; pipeline.DepthStencilState = depth; pipeline.SampleMask = UINT_MAX; pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pipeline.NumRenderTargets = 1; pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT; pipeline.SampleDesc.Count = 1;
    check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&pipelineState_)), "Could not create graphics pipeline.");
    const UINT64 constantSize = (sizeof(CameraConstants) + 255u) & ~255u; check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(constantSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer_)), "Could not create constant buffer.");
    check(constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedConstants_)), "Could not map constant buffer.");
}

void D3D12Renderer::uploadMesh(const voxel::Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;
    const UINT64 vertexBytes = static_cast<UINT64>(mesh.vertices.size() * sizeof(voxel::Vertex)); const UINT64 indexBytes = static_cast<UINT64>(mesh.indices.size() * sizeof(std::uint32_t));
    check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(vertexBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer_)), "Could not create vertex buffer.");
    check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(indexBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer_)), "Could not create index buffer.");
    void* mapped = nullptr; check(vertexBuffer_->Map(0, nullptr, &mapped), "Could not map vertex buffer."); memcpy(mapped, mesh.vertices.data(), vertexBytes); vertexBuffer_->Unmap(0, nullptr); check(indexBuffer_->Map(0, nullptr, &mapped), "Could not map index buffer."); memcpy(mapped, mesh.indices.data(), indexBytes); indexBuffer_->Unmap(0, nullptr);
    vertexView_ = {vertexBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), sizeof(voxel::Vertex)}; indexView_ = {indexBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT}; indexCount_ = static_cast<UINT>(mesh.indices.size());
}
void D3D12Renderer::setMesh(const voxel::Mesh& mesh) { waitForGpu(); uploadMesh(mesh); }

void D3D12Renderer::render(const XMMATRIX& viewProjection, const std::filesystem::path& screenshotPath) {
    check(commandAllocator_->Reset(), "Could not reset command allocator."); check(commandList_->Reset(commandAllocator_.Get(), pipelineState_.Get()), "Could not reset command list.");
    const XMMATRIX transposed = XMMatrixTranspose(viewProjection); XMStoreFloat4x4(&mappedConstants_->viewProjection, transposed);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = renderTargets_[frameIndex_].Get(); barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; commandList_->ResourceBarrier(1, &barrier);
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart(); commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsvHandle); const float clear[] = {0.08f, 0.12f, 0.2f, 1.0f}; commandList_->ClearRenderTargetView(rtv, clear, 0, nullptr); commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1}; D3D12_RECT scissor{0, 0, width_, height_}; commandList_->RSSetViewports(1, &viewport); commandList_->RSSetScissorRects(1, &scissor); commandList_->SetGraphicsRootSignature(rootSignature_.Get()); commandList_->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commandList_->IASetVertexBuffers(0, 1, &vertexView_); commandList_->IASetIndexBuffer(&indexView_); commandList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = renderTest_ ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_PRESENT; commandList_->ResourceBarrier(1, &barrier);
    if (renderTest_ && !screenshotPath.empty()) { captureBackBuffer(screenshotPath); return; }
    check(commandList_->Close(), "Could not close command list."); ID3D12CommandList* lists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, lists); check(swapchain_->Present(1, 0), "Could not present frame."); frameIndex_ = swapchain_->GetCurrentBackBufferIndex(); waitForGpu();
}

void D3D12Renderer::captureBackBuffer(const std::filesystem::path& path) {
    D3D12_RESOURCE_DESC source = renderTargets_[frameIndex_]->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 rowBytes{}, totalBytes{}; device_->GetCopyableFootprints(&source, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
    ComPtr<ID3D12Resource> readback; check(device_->CreateCommittedResource(&readbackHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(totalBytes), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Could not create screenshot readback buffer.");
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint; D3D12_TEXTURE_COPY_LOCATION sourceLocation{}; sourceLocation.pResource = renderTargets_[frameIndex_].Get(); sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sourceLocation.SubresourceIndex = 0; commandList_->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
    check(commandList_->Close(), "Could not close screenshot command list."); ID3D12CommandList* lists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, lists); waitForGpu();
    void* data = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(totalBytes)}; check(readback->Map(0, &range, &data), "Could not map screenshot readback.");
    BITMAPFILEHEADER fileHeader{}; BITMAPINFOHEADER infoHeader{}; const int rowSize = width_ * 3; const int paddedRow = (rowSize + 3) & ~3; fileHeader.bfType = 0x4D42; fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader); fileHeader.bfSize = fileHeader.bfOffBits + paddedRow * height_; infoHeader.biSize = sizeof(infoHeader); infoHeader.biWidth = width_; infoHeader.biHeight = height_; infoHeader.biPlanes = 1; infoHeader.biBitCount = 24; infoHeader.biCompression = BI_RGB;
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader)); output.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader)); const auto* pixels = static_cast<const std::uint8_t*>(data); std::vector<std::uint8_t> row(static_cast<std::size_t>(paddedRow)); for (int y = height_ - 1; y >= 0; --y) { for (int x = 0; x < width_; ++x) { const auto* pixel = pixels + footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch + x * 4; row[x * 3] = pixel[2]; row[x * 3 + 1] = pixel[1]; row[x * 3 + 2] = pixel[0]; } output.write(reinterpret_cast<const char*>(row.data()), paddedRow); } readback->Unmap(0, nullptr);
}

void D3D12Renderer::waitForGpu() { const UINT64 value = ++fenceValue_; check(commandQueue_->Signal(fence_.Get(), value), "Could not signal GPU fence."); if (fence_->GetCompletedValue() < value) { check(fence_->SetEventOnCompletion(value, fenceEvent_), "Could not set fence event."); WaitForSingleObject(fenceEvent_, INFINITE); } }
} // namespace directcraft::renderer



