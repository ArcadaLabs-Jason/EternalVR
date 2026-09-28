// The additive wash filter on the XR worker (ui_wash.hpp).

#include "vkcore/ui_wash.hpp"

#include "ui_layer/additive_wash.hpp"

#include <d3dcompiler.h>

#include <array>
#include <cstddef>
#include <cstring>

namespace evr::vkcore {

namespace {

using Microsoft::WRL::ComPtr;

// ui_layer::removeAdditiveWash on the GPU. Bytes are handled as bytes: a UNORM load is byte / 255, turned
// back with round(v * 255), and the stores write whole bytes, so the result is the reference's exactly.
// kWashBlock is 64: BlockFloor runs one 16x16 group per block, each thread over 4x4 pixels.
constexpr char kShader[] = R"hlsl(
cbuffer Size : register(b0) { uint width; uint height; uint blocksX; uint blocksY; };
Texture2D<float4> source : register(t0);
RWTexture2D<uint> floors : register(u0);
RWTexture2D<unorm float4> output : register(u1);

uint4 bytesAt(uint2 p) { return (uint4)round(source.Load(int3(p, 0)) * 255.0); }
uint3 excessOf(uint4 c) { return (uint3)max((int3)c.rgb - (int)c.a, 0); }

groupshared uint floorR;
groupshared uint floorG;
groupshared uint floorB;

[numthreads(16, 16, 1)]
void BlockFloor(uint3 block : SV_GroupID, uint3 t : SV_GroupThreadID, uint index : SV_GroupIndex) {
    if (index == 0) { floorR = 255; floorG = 255; floorB = 255; }
    GroupMemoryBarrierWithGroupSync();
    uint3 low = uint3(255, 255, 255);
    for (uint y = 0; y < 4; ++y) {
        for (uint x = 0; x < 4; ++x) {
            uint2 p = block.xy * 64 + t.xy * 4 + uint2(x, y);
            if (p.x < width && p.y < height) { low = min(low, excessOf(bytesAt(p))); }
        }
    }
    InterlockedMin(floorR, low.r);
    InterlockedMin(floorG, low.g);
    InterlockedMin(floorB, low.b);
    GroupMemoryBarrierWithGroupSync();
    if (index == 0) { floors[block.xy] = floorR | (floorG << 8) | (floorB << 16); }
}

[numthreads(8, 8, 1)]
void Apply(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) { return; }
    uint2 b = id.xy / 64;
    uint3 wash = uint3(0, 0, 0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 n = int2(b) + int2(dx, dy);
            if (n.x >= 0 && n.y >= 0 && n.x < (int)blocksX && n.y < (int)blocksY) {
                uint f = floors[uint2(n)];
                wash = max(wash, uint3(f & 255, (f >> 8) & 255, (f >> 16) & 255));
            }
        }
    }
    uint4 c = bytesAt(id.xy);
    uint3 rgb = c.rgb - min(wash, excessOf(c));
    output[id.xy] = float4(float3(rgb) / 255.0, float(c.a) / 255.0);
}
)hlsl";

using CompileFn = HRESULT(WINAPI*)(LPCVOID,
                                   SIZE_T,
                                   LPCSTR,
                                   const D3D_SHADER_MACRO*,
                                   ID3DInclude*,
                                   LPCSTR,
                                   LPCSTR,
                                   UINT,
                                   UINT,
                                   ID3DBlob**,
                                   ID3DBlob**);

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

D3D12_RESOURCE_BARRIER uavBarrier(ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = r;
    return b;
}

} // namespace

bool UiWash::create(ID3D12Device* device, const char*& why) {
    destroy();
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile =
        compiler ? reinterpret_cast<CompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!compile) {
        why = "d3dcompiler_47.dll or its D3DCompile not found";
        return false;
    }
    std::array<ComPtr<ID3DBlob>, 2> code;
    const std::array<const char*, 2> entries{"BlockFloor", "Apply"};
    for (std::size_t i = 0; i < code.size(); ++i) {
        ComPtr<ID3DBlob> errors;
        if (FAILED(compile(kShader, std::strlen(kShader), "ui_wash", nullptr, nullptr, entries[i], "cs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code[i], &errors))) {
            why = "the shader did not compile";
            return false;
        }
    }
    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 2;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    std::array<D3D12_ROOT_PARAMETER, 2> params{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
    params[1].DescriptorTable.pDescriptorRanges = ranges.data();
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    ComPtr<ID3DBlob> rootBlob;
    ComPtr<ID3DBlob> rootErrors;
    if (FAILED(
            D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &rootErrors)) ||
        FAILED(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature_)))) {
        why = "the root signature could not be made";
        destroy();
        return false;
    }
    std::array<ComPtr<ID3D12PipelineState>*, 2> pipelines{&blockPipeline_, &applyPipeline_};
    for (std::size_t i = 0; i < code.size(); ++i) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature_.Get();
        desc.CS = {code[i]->GetBufferPointer(), code[i]->GetBufferSize()};
        if (FAILED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&*pipelines[i])))) {
            why = "a compute pipeline could not be made";
            destroy();
            return false;
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 3;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_)))) {
        why = "the descriptor heap could not be made";
        destroy();
        return false;
    }
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device_ = device;
    return true;
}

bool UiWash::makeImages(std::uint32_t width, std::uint32_t height) {
    if (output_ && width == width_ && height == height_) {
        return true;
    }
    floors_.Reset();
    output_.Reset();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Width = ui_layer::washBlocks(width);
    desc.Height = ui_layer::washBlocks(height);
    desc.Format = DXGI_FORMAT_R32_UINT;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&floors_)))) {
        return false;
    }
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&output_)))) {
        floors_.Reset();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

ID3D12Resource* UiWash::record(ID3D12GraphicsCommandList* list,
                               ID3D12Resource* source,
                               std::uint32_t width,
                               std::uint32_t height) {
    if (!ready() || width == 0 || height == 0) {
        return nullptr;
    }
    if (!makeImages(width, height)) {
        return nullptr;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(source, &srv, cpu);
    cpu.ptr += descriptorSize_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format = DXGI_FORMAT_R32_UINT;
    device_->CreateUnorderedAccessView(floors_.Get(), nullptr, &uav, cpu);
    cpu.ptr += descriptorSize_;
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device_->CreateUnorderedAccessView(output_.Get(), nullptr, &uav, cpu);

    const D3D12_RESOURCE_BARRIER readable =
        transition(source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &readable);
    ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rootSignature_.Get());
    const std::uint32_t blocksX = ui_layer::washBlocks(width);
    const std::uint32_t blocksY = ui_layer::washBlocks(height);
    const std::array<std::uint32_t, 4> size{width, height, blocksX, blocksY};
    list->SetComputeRoot32BitConstants(0, static_cast<UINT>(size.size()), size.data(), 0);
    list->SetComputeRootDescriptorTable(1, heap_->GetGPUDescriptorHandleForHeapStart());
    list->SetPipelineState(blockPipeline_.Get());
    list->Dispatch(blocksX, blocksY, 1);
    const D3D12_RESOURCE_BARRIER floorsWritten = uavBarrier(floors_.Get());
    list->ResourceBarrier(1, &floorsWritten);
    list->SetPipelineState(applyPipeline_.Get());
    list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    const std::array<D3D12_RESOURCE_BARRIER, 2> after{
        transition(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        transition(output_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE)};
    list->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
    return output_.Get();
}

void UiWash::afterCopy(ID3D12GraphicsCommandList* list) {
    const D3D12_RESOURCE_BARRIER back =
        transition(output_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->ResourceBarrier(1, &back);
}

void UiWash::destroy() {
    floors_.Reset();
    output_.Reset();
    heap_.Reset();
    blockPipeline_.Reset();
    applyPipeline_.Reset();
    rootSignature_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

} // namespace evr::vkcore
