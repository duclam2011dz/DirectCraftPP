#include "renderer/d3d12_renderer.h"

#include <d3dcompiler.h>
#include <cstring>
#include <windows.h>
#include <wincodec.h>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>

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
    createDevice(renderTest); createSwapchain(window); createTargets(); createPipeline(); createQueries();
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
    ComPtr<ID3D12Debug1> debug1; if (debug && SUCCEEDED(debug.As(&debug1))) { debug1->SetEnableGPUBasedValidation(TRUE); gpuValidationEnabled_ = true; }
    flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) { dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON); dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON); dredEnabled_ = true; }
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
    adapter_->GetDesc1(&adapterDescription_);
    usingWarp_ = (adapterDescription_.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
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
    if (FAILED(result)) { const std::string detail = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string("unknown shader error"); if (errors) OutputDebugStringA(detail.c_str()); throw std::runtime_error(std::string("HLSL shader compilation failed: ") + detail); }
    return shader;
}

void D3D12Renderer::createPipeline() {
    D3D12_ROOT_PARAMETER parameters[2]{}; parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[0].Descriptor.ShaderRegister = 0; parameters[0].Descriptor.RegisterSpace = 0; parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; D3D12_DESCRIPTOR_RANGE textureRange{}; textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; textureRange.NumDescriptors = 1; textureRange.BaseShaderRegister = 0; textureRange.OffsetInDescriptorsFromTableStart = 0; parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; parameters[1].DescriptorTable.NumDescriptorRanges = 1; parameters[1].DescriptorTable.pDescriptorRanges = &textureRange; parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{}; rootDescription.NumParameters = 2; rootDescription.pParameters = parameters; rootDescription.NumStaticSamplers = 1; rootDescription.pStaticSamplers = &sampler; rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized; ComPtr<ID3DBlob> errors; check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Could not serialize root signature.");
    check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), "Could not create root signature.");
    const std::filesystem::path shaderPath = std::filesystem::path(DIRECTCRAFT_SHADER_DIR) / "voxel.hlsl";
    const auto vertexShader = compileShader(shaderPath, "VSMain", "vs_5_0"); const auto packedVertexShader = compileShader(shaderPath, "VSMainPacked", "vs_5_0"); const auto pixelShader = compileShader(shaderPath, "PSMain", "ps_5_0");
    D3D12_INPUT_ELEMENT_DESC input[] = {{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"AO",0,DXGI_FORMAT_R32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"MATERIAL",0,DXGI_FORMAT_R32_UINT,0,52,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_RASTERIZER_DESC rasterizer{}; rasterizer.FillMode = wireframe_ ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID; rasterizer.CullMode = D3D12_CULL_MODE_NONE; rasterizer.DepthClipEnable = TRUE; D3D12_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; D3D12_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS; D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature = rootSignature_.Get(); pipeline.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()}; pipeline.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()}; pipeline.InputLayout = {input, _countof(input)}; pipeline.RasterizerState = rasterizer; pipeline.BlendState = blend; pipeline.DepthStencilState = depth; pipeline.SampleMask = UINT_MAX; pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pipeline.NumRenderTargets = 1; pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT; pipeline.SampleDesc.Count = 1;
    auto linePipeline = pipeline; linePipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; linePipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; linePipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; linePipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; linePipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; check(device_->CreateGraphicsPipelineState(&linePipeline, IID_PPV_ARGS(&outlinePipelineState_)), "Could not create outline pipeline.");
    auto overlayPipeline = pipeline; overlayPipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; overlayPipeline.DepthStencilState.DepthEnable = FALSE; overlayPipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; overlayPipeline.BlendState.RenderTarget[0].BlendEnable = TRUE; overlayPipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA; overlayPipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; overlayPipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; overlayPipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE; overlayPipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; overlayPipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD; check(device_->CreateGraphicsPipelineState(&overlayPipeline, IID_PPV_ARGS(&overlayPipelineState_)), "Could not create overlay pipeline.");
    check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&pipelineState_)), "Could not create float graphics pipeline.");
    auto packedPipeline = pipeline; D3D12_INPUT_ELEMENT_DESC packedInput[] = {{"PACKED_POSITION",0,DXGI_FORMAT_R32_UINT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"PACKED_ATTRIBUTES",0,DXGI_FORMAT_R32_UINT,0,4,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}}; packedPipeline.VS = {packedVertexShader->GetBufferPointer(), packedVertexShader->GetBufferSize()}; packedPipeline.InputLayout = {packedInput, _countof(packedInput)}; check(device_->CreateGraphicsPipelineState(&packedPipeline, IID_PPV_ARGS(&packedPipelineState_)), "Could not create packed graphics pipeline.");
    const UINT64 constantSize = (sizeof(CameraConstants) + 255u) & ~255u; check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(constantSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer_)), "Could not create constant buffer.");
    check(constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedConstants_)), "Could not map constant buffer.");
}

void D3D12Renderer::createQueries() {
    D3D12_QUERY_HEAP_DESC description{}; description.Count = 2; description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    if (FAILED(device_->CreateQueryHeap(&description, IID_PPV_ARGS(&timestampHeap_)))) return;
    if (FAILED(commandQueue_->GetTimestampFrequency(&timestampFrequency_))) { timestampHeap_.Reset(); return; }
    if (FAILED(device_->CreateCommittedResource(&readbackHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(sizeof(UINT64) * 2), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&timestampReadback_)))) { timestampHeap_.Reset(); return; }
    gpuTimestampAvailable_ = timestampFrequency_ != 0;
}

void D3D12Renderer::setTextureAtlas(const voxel::TextureAtlas& atlas) {
    if (atlas.pixels.empty()) return;
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{}; heapDescription.NumDescriptors = 1; heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; check(device_->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&textureHeap_)), "Could not create texture descriptor heap.");
    D3D12_RESOURCE_DESC description{}; description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; description.Width = static_cast<UINT64>(atlas.tileSize * atlas.tilesX); description.Height = static_cast<UINT>(atlas.tileSize * atlas.tilesY); description.DepthOrArraySize = 1; description.MipLevels = 1; description.Format = DXGI_FORMAT_R8G8B8A8_UNORM; description.SampleDesc.Count = 1; description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES defaultProperties{}; defaultProperties.Type = D3D12_HEAP_TYPE_DEFAULT; check(device_->CreateCommittedResource(&defaultProperties, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureAtlas_)), "Could not create procedural texture atlas.");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 rowBytes{}; UINT64 totalBytes{}; device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes); check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(totalBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUpload_)), "Could not create texture upload buffer.");
    void* mapped = nullptr; check(textureUpload_->Map(0, nullptr, &mapped), "Could not map procedural texture atlas."); for (UINT row = 0; row < rows; ++row) std::memcpy(static_cast<std::uint8_t*>(mapped) + footprint.Offset + static_cast<SIZE_T>(row) * footprint.Footprint.RowPitch, atlas.pixels.data() + static_cast<std::size_t>(row) * atlas.tileSize * atlas.tilesX, static_cast<std::size_t>(atlas.tileSize * atlas.tilesX) * sizeof(std::uint32_t)); textureUpload_->Unmap(0, nullptr);
    check(commandAllocator_->Reset(), "Could not reset texture upload allocator."); check(commandList_->Reset(commandAllocator_.Get(), nullptr), "Could not reset texture upload list."); D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = textureAtlas_.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = textureUpload_.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint; commandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr); D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = textureAtlas_.Get(); barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; commandList_->ResourceBarrier(1, &barrier); check(commandList_->Close(), "Could not close texture upload list."); ID3D12CommandList* lists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, lists); waitForGpu();
    D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = DXGI_FORMAT_R8G8B8A8_UNORM; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1; device_->CreateShaderResourceView(textureAtlas_.Get(), &view, textureHeap_->GetCPUDescriptorHandleForHeapStart()); textureHandle_ = textureHeap_->GetGPUDescriptorHandleForHeapStart();
}

void D3D12Renderer::uploadMesh(const voxel::Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;
    const auto packed = mesh.packedVertices.empty() ? [&mesh] { std::vector<voxel::PackedVertex> result; result.reserve(mesh.vertices.size()); for (const auto& vertex : mesh.vertices) result.push_back(voxel::World::packVertex(vertex)); return result; }() : mesh.packedVertices; const UINT64 vertexBytes = static_cast<UINT64>(packed.size() * sizeof(voxel::PackedVertex)); const UINT64 indexBytes = static_cast<UINT64>(mesh.indices.size() * sizeof(std::uint32_t)); vertexBytes_ = vertexBytes; indexBytes_ = indexBytes;
    check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(vertexBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&packedVertexBuffer_)), "Could not create packed vertex buffer.");
    const UINT64 unpackedBytes = static_cast<UINT64>(mesh.vertices.size() * sizeof(voxel::Vertex));
    check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(unpackedBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer_)), "Could not create vertex buffer.");
    check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(indexBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer_)), "Could not create index buffer.");
    void* mapped = nullptr; check(packedVertexBuffer_->Map(0, nullptr, &mapped), "Could not map packed vertex buffer."); memcpy(mapped, packed.data(), vertexBytes); packedVertexBuffer_->Unmap(0, nullptr); check(vertexBuffer_->Map(0, nullptr, &mapped), "Could not map vertex buffer."); memcpy(mapped, mesh.vertices.data(), unpackedBytes); vertexBuffer_->Unmap(0, nullptr); check(indexBuffer_->Map(0, nullptr, &mapped), "Could not map index buffer."); memcpy(mapped, mesh.indices.data(), indexBytes); indexBuffer_->Unmap(0, nullptr);
    packedVertexView_ = {packedVertexBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), sizeof(voxel::PackedVertex)}; vertexView_ = {vertexBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(unpackedBytes), sizeof(voxel::Vertex)}; indexView_ = {indexBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT}; indexCount_ = static_cast<UINT>(mesh.indices.size());
}
void D3D12Renderer::setMesh(const voxel::Mesh& mesh) { waitForGpu(); uploadMesh(mesh); }
void D3D12Renderer::uploadOutline(const std::vector<voxel::Vertex>& vertices) {
    outlineVertexCount_ = static_cast<UINT>(vertices.size()); outlineBuffer_.Reset(); if (vertices.empty()) return;
    const UINT64 bytes = vertices.size() * sizeof(voxel::Vertex); check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(bytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outlineBuffer_)), "Could not create outline buffer."); void* mapped = nullptr; check(outlineBuffer_->Map(0, nullptr, &mapped), "Could not map outline buffer."); memcpy(mapped, vertices.data(), bytes); outlineBuffer_->Unmap(0, nullptr); outlineView_ = {outlineBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(bytes), sizeof(voxel::Vertex)};
}
void D3D12Renderer::setSelectionOutline(const voxel::Int3* block) {
    if (!block) { outlineVertexCount_ = 0; outlineBuffer_.Reset(); return; }
    const float x = static_cast<float>(block->x) - 0.002f, y = static_cast<float>(block->y) - 0.002f, z = static_cast<float>(block->z) - 0.002f, s = 1.004f;
    const std::array<std::array<float, 3>, 8> p = {{{x,y,z},{x+s,y,z},{x+s,y+s,z},{x,y+s,z},{x,y,z+s},{x+s,y,z+s},{x+s,y+s,z+s},{x,y+s,z+s}}};
    const std::array<std::array<int, 2>, 12> edges = {{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}}};
    std::vector<voxel::Vertex> vertices; vertices.reserve(24); for (const auto& edge : edges) for (int index : edge) { voxel::Vertex v{}; v.position[0]=p[index][0]; v.position[1]=p[index][1]; v.position[2]=p[index][2]; v.uv[0]=0; v.color[3] = -1.0f; vertices.push_back(v); } uploadOutline(vertices);
}
void D3D12Renderer::uploadOverlay(const std::vector<voxel::Vertex>& vertices) {
    overlayVertexCount_ = static_cast<UINT>(vertices.size()); overlayBuffer_.Reset(); if (vertices.empty()) return; const UINT64 bytes = vertices.size() * sizeof(voxel::Vertex); check(device_->CreateCommittedResource(&uploadHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(bytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&overlayBuffer_)), "Could not create overlay buffer."); void* mapped = nullptr; check(overlayBuffer_->Map(0, nullptr, &mapped), "Could not map overlay buffer."); memcpy(mapped, vertices.data(), bytes); overlayBuffer_->Unmap(0, nullptr); overlayView_ = {overlayBuffer_->GetGPUVirtualAddress(), static_cast<UINT>(bytes), sizeof(voxel::Vertex)};
}
void D3D12Renderer::setDevToolsOverlay(bool enabled, const std::vector<std::string>& lines) {
    if (!enabled) { overlayVertexCount_ = 0; overlayBuffer_.Reset(); return; }
    std::vector<voxel::Vertex> vertices; const float left=-0.98f, top=0.94f, charW=0.010f, charH=0.018f;
    auto quad = [&vertices](float x0,float y0,float x1,float y1,float r,float g,float b,float a) { const std::array<std::array<float,2>,6> points = {{{x0,y0},{x1,y0},{x1,y1},{x0,y0},{x1,y1},{x0,y1}}}; for (const auto& p : points) { voxel::Vertex v{}; v.position[0]=p[0]; v.position[1]=p[1]; v.position[2]=0; v.color[0]=r; v.color[1]=g; v.color[2]=b; v.color[3]=a; v.uv[0]=-1; vertices.push_back(v); } };
    quad(left, top-std::min(0.72f, 0.035f*static_cast<float>(lines.size()+1)), 0.02f, top+0.025f, 0.01f,0.02f,0.04f,0.82f);
    for (std::size_t row=0; row<lines.size(); ++row) { const auto& line=lines[row]; for (std::size_t col=0; col<line.size() && col<90; ++col) if (line[col] != ' ') { const float x=left+0.012f+static_cast<float>(col)*charW, y=top-0.012f-static_cast<float>(row)*0.035f; const unsigned code=static_cast<unsigned char>(line[col]); const float shade=0.55f+static_cast<float>(code%4)*0.1f; quad(x,y-charH,x+charW*0.65f,y,shade,shade,0.95f,1.0f); } }
    uploadOverlay(vertices);
}
void D3D12Renderer::setWireframe(bool enabled) { if (wireframe_ == enabled) return; waitForGpu(); if (mappedConstants_) { constantBuffer_->Unmap(0, nullptr); mappedConstants_ = nullptr; } wireframe_ = enabled; createPipeline(); }
std::string D3D12Renderer::adapterName() const { char result[256]{}; WideCharToMultiByte(CP_UTF8, 0, adapterDescription_.Description, -1, result, sizeof(result), nullptr, nullptr); return result; }

void D3D12Renderer::render(const XMMATRIX& viewProjection, const std::filesystem::path& screenshotPath) {
    check(commandAllocator_->Reset(), "Could not reset command allocator."); check(commandList_->Reset(commandAllocator_.Get(), packedPipelineState_.Get()), "Could not reset command list.");
    const XMMATRIX transposed = XMMatrixTranspose(viewProjection); XMStoreFloat4x4(&mappedConstants_->viewProjection, transposed);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = renderTargets_[frameIndex_].Get(); barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; commandList_->ResourceBarrier(1, &barrier);
    if (gpuTimestampAvailable_) commandList_->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    const char frameEvent[] = "DirectCraft Frame"; commandList_->BeginEvent(0, frameEvent, static_cast<UINT>(sizeof(frameEvent) - 1));
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart(); commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsvHandle); const float clear[] = {0.35f, 0.65f, 0.95f, 1.0f}; commandList_->ClearRenderTargetView(rtv, clear, 0, nullptr); commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1}; D3D12_RECT scissor{0, 0, width_, height_}; commandList_->RSSetViewports(1, &viewport); commandList_->RSSetScissorRects(1, &scissor); commandList_->SetGraphicsRootSignature(rootSignature_.Get()); if (textureHeap_) { ID3D12DescriptorHeap* heaps[] = {textureHeap_.Get()}; commandList_->SetDescriptorHeaps(1, heaps); commandList_->SetGraphicsRootDescriptorTable(1, textureHandle_); } commandList_->SetPipelineState(packedPipelineState_.Get()); commandList_->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commandList_->IASetVertexBuffers(0, 1, &packedVertexView_); commandList_->IASetIndexBuffer(&indexView_); const char sceneEvent[] = "Scene"; commandList_->BeginEvent(0, sceneEvent, static_cast<UINT>(sizeof(sceneEvent)-1)); commandList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0); commandList_->EndEvent();
    if (outlineVertexCount_) { const char outlineEvent[] = "Selection Outline"; commandList_->BeginEvent(0, outlineEvent, static_cast<UINT>(sizeof(outlineEvent)-1)); commandList_->SetPipelineState(outlinePipelineState_.Get()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST); commandList_->IASetVertexBuffers(0, 1, &outlineView_); commandList_->DrawInstanced(outlineVertexCount_, 1, 0, 0); commandList_->EndEvent(); }
    if (overlayVertexCount_) { const char overlayEvent[] = "DevTools Overlay"; commandList_->BeginEvent(0, overlayEvent, static_cast<UINT>(sizeof(overlayEvent)-1)); XMStoreFloat4x4(&mappedConstants_->viewProjection, XMMatrixIdentity()); commandList_->SetPipelineState(overlayPipelineState_.Get()); commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commandList_->IASetVertexBuffers(0, 1, &overlayView_); commandList_->DrawInstanced(overlayVertexCount_, 1, 0, 0); commandList_->EndEvent(); }
    commandList_->EndEvent();
    if (gpuTimestampAvailable_) { commandList_->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1); commandList_->ResolveQueryData(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timestampReadback_.Get(), 0); }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = !screenshotPath.empty() ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_PRESENT; commandList_->ResourceBarrier(1, &barrier);
    if (!screenshotPath.empty()) { captureBackBuffer(screenshotPath); return; }
    check(commandList_->Close(), "Could not close command list."); ID3D12CommandList* lists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, lists); check(swapchain_->Present(1, 0), "Could not present frame."); frameIndex_ = swapchain_->GetCurrentBackBufferIndex(); waitForGpu(); if (gpuTimestampAvailable_) { UINT64* values=nullptr; D3D12_RANGE range{0,sizeof(UINT64)*2}; if (SUCCEEDED(timestampReadback_->Map(0,&range,reinterpret_cast<void**>(&values))) && values[1]>=values[0]) { gpuFrameMs_=1000.0*static_cast<double>(values[1]-values[0])/static_cast<double>(timestampFrequency_); timestampReadback_->Unmap(0,nullptr); } }
}

void D3D12Renderer::captureBackBuffer(const std::filesystem::path& path) {
    D3D12_RESOURCE_DESC source = renderTargets_[frameIndex_]->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 rowBytes{}, totalBytes{}; device_->GetCopyableFootprints(&source, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
    ComPtr<ID3D12Resource> readback; check(device_->CreateCommittedResource(&readbackHeap(), D3D12_HEAP_FLAG_NONE, &bufferDesc(totalBytes), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Could not create screenshot readback buffer.");
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint; D3D12_TEXTURE_COPY_LOCATION sourceLocation{}; sourceLocation.pResource = renderTargets_[frameIndex_].Get(); sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sourceLocation.SubresourceIndex = 0; commandList_->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
    check(commandList_->Close(), "Could not close screenshot command list."); ID3D12CommandList* lists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, lists); waitForGpu();
    void* data = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(totalBytes)}; check(readback->Map(0, &range, &data), "Could not map screenshot readback.");
    BITMAPFILEHEADER fileHeader{}; BITMAPINFOHEADER infoHeader{}; const int rowSize = width_ * 3; const int paddedRow = (rowSize + 3) & ~3; fileHeader.bfType = 0x4D42; fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader); fileHeader.bfSize = fileHeader.bfOffBits + paddedRow * height_; infoHeader.biSize = sizeof(infoHeader); infoHeader.biWidth = width_; infoHeader.biHeight = height_; infoHeader.biPlanes = 1; infoHeader.biBitCount = 24; infoHeader.biCompression = BI_RGB;
    const auto* pixels = static_cast<const std::uint8_t*>(data);
    const auto restoreBackBuffer = [this]() { check(commandAllocator_->Reset(), "Could not reset capture allocator."); check(commandList_->Reset(commandAllocator_.Get(), pipelineState_.Get()), "Could not reset capture command list."); D3D12_RESOURCE_BARRIER restore{}; restore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; restore.Transition.pResource = renderTargets_[frameIndex_].Get(); restore.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; restore.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT; restore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; commandList_->ResourceBarrier(1, &restore); check(commandList_->Close(), "Could not close capture restore list."); ID3D12CommandList* restoreLists[] = {commandList_.Get()}; commandQueue_->ExecuteCommandLists(1, restoreLists); waitForGpu(); };
    if (path.extension() == L".png") { writePng(path, pixels, footprint.Footprint.RowPitch); readback->Unmap(0, nullptr); restoreBackBuffer(); return; }
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader)); output.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader)); std::vector<std::uint8_t> row(static_cast<std::size_t>(paddedRow)); for (int y = height_ - 1; y >= 0; --y) { for (int x = 0; x < width_; ++x) { const auto* pixel = pixels + footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch + x * 4; row[x * 3] = pixel[2]; row[x * 3 + 1] = pixel[1]; row[x * 3 + 2] = pixel[0]; } output.write(reinterpret_cast<const char*>(row.data()), paddedRow); } readback->Unmap(0, nullptr); restoreBackBuffer();
}

void D3D12Renderer::writePng(const std::filesystem::path& path, const void* pixels, UINT rowPitch) {
    ComPtr<IWICImagingFactory> factory; check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Could not create WIC factory.");
    ComPtr<IWICStream> stream; check(factory->CreateStream(&stream), "Could not create WIC stream."); check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Could not open PNG output.");
    ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Could not create PNG encoder."); check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Could not initialize PNG encoder.");
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties; check(encoder->CreateNewFrame(&frame, &properties), "Could not create PNG frame."); check(frame->Initialize(properties.Get()), "Could not initialize PNG frame."); check(frame->SetSize(static_cast<UINT>(width_), static_cast<UINT>(height_)), "Could not set PNG size."); WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; check(frame->SetPixelFormat(&format), "Could not set PNG format."); check(frame->WritePixels(static_cast<UINT>(height_), rowPitch, rowPitch * static_cast<UINT>(height_), static_cast<BYTE*>(const_cast<void*>(pixels))), "Could not write PNG pixels."); check(frame->Commit(), "Could not commit PNG frame."); check(encoder->Commit(), "Could not commit PNG.");
}

void D3D12Renderer::waitForGpu() { const UINT64 value = ++fenceValue_; check(commandQueue_->Signal(fence_.Get(), value), "Could not signal GPU fence."); if (fence_->GetCompletedValue() < value) { check(fence_->SetEventOnCompletion(value, fenceEvent_), "Could not set fence event."); WaitForSingleObject(fenceEvent_, INFINITE); } }
} // namespace directcraft::renderer
