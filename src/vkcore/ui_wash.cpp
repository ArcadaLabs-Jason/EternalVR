// The additive wash filter on the XR worker (ui_wash.hpp).

#include "vkcore/ui_wash.hpp"

#include "ui_layer/additive_wash.hpp"
#include "vkcore/log.hpp"

#include <d3dcompiler.h>

#include <array>
#include <cstddef>
#include <cstring>

namespace evr::vkcore {

namespace {

using Microsoft::WRL::ComPtr;

// ui_layer::removeAdditiveWash on the GPU. Bytes are handled as bytes: a UNORM load is byte / 255, turned
// back with round(v * 255), the bound, the share and the interpolation are the reference's integer
// arithmetic and the stores write whole bytes, so the result is the reference's exactly. kWashBlock is 64
// (kWashSpan 128, kWashLitRatio 2): BlockFloor runs one 16x16 group per block, each thread over 4x4 pixels;
// BlockLevel one thread per block; Apply one thread per pixel.
static_assert(ui_layer::kWashBlock == 64 && ui_layer::kWashSpan == 128 && ui_layer::kWashLitRatio == 2,
              "the HLSL below writes these constants out: change it with them");
constexpr char kShader[] = R"hlsl(
cbuffer Size : register(b0) { uint width; uint height; uint blocksX; uint blocksY; };
Texture2D<float4> source : register(t0);
RWTexture2D<uint> floors : register(u0);   // r | g << 8 | b << 16 | known << 24
RWTexture2D<unorm float4> output : register(u1);
RWTexture2D<uint> levels : register(u2);   // r | g << 8 | b << 16

uint4 bytesAt(uint2 p) { return (uint4)round(source.Load(int3(p, 0)) * 255.0); }
uint3 boundOf(uint4 c) {
    return c.a == 255 ? uint3(255, 255, 255) : min((2 * c.rgb + 1) * 255 / (2 * max(255 - c.a, 1u)), 255);
}
uint3 unpack(uint v) { return uint3(v & 255, (v >> 8) & 255, (v >> 16) & 255); }

groupshared uint floorR;
groupshared uint floorG;
groupshared uint floorB;
groupshared uint known;

[numthreads(16, 16, 1)]
void BlockFloor(uint3 block : SV_GroupID, uint3 t : SV_GroupThreadID, uint index : SV_GroupIndex) {
    if (index == 0) { floorR = 255; floorG = 255; floorB = 255; known = 0; }
    GroupMemoryBarrierWithGroupSync();
    uint3 low = uint3(255, 255, 255);
    uint uncovered = 0;
    for (uint y = 0; y < 4; ++y) {
        for (uint x = 0; x < 4; ++x) {
            uint2 p = block.xy * 64 + t.xy * 4 + uint2(x, y);
            if (p.x < width && p.y < height) {
                uint4 c = bytesAt(p);
                low = min(low, boundOf(c));
                if (c.a == 0) { uncovered = 1; }
            }
        }
    }
    InterlockedMin(floorR, low.r);
    InterlockedMin(floorG, low.g);
    InterlockedMin(floorB, low.b);
    InterlockedOr(known, uncovered);
    GroupMemoryBarrierWithGroupSync();
    if (index == 0) { floors[block.xy] = floorR | (floorG << 8) | (floorB << 16) | (known << 24); }
}

[numthreads(8, 8, 1)]
void BlockLevel(uint3 id : SV_DispatchThreadID) {
    if (id.x >= blocksX || id.y >= blocksY) { return; }
    uint3 most = uint3(0, 0, 0);
    uint3 lit = uint3(0, 0, 0);
    uint3 unlit = uint3(0, 0, 0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 n = int2(id.xy) + int2(dx, dy);
            if (n.x >= 0 && n.y >= 0 && n.x < (int)blocksX && n.y < (int)blocksY) {
                uint f = floors[uint2(n)];
                if ((f >> 24) != 0) {
                    uint3 light = unpack(f);
                    most = max(most, light);
                    lit += light > 0 ? 1 : 0;
                    unlit += light > 0 ? 0 : 1;
                }
            }
        }
    }
    uint3 level = lit >= 2 * unlit ? most : uint3(0, 0, 0);
    levels[id.xy] = level.r | (level.g << 8) | (level.b << 16);
}

// Where a pixel sits between block centres along one axis (additive_wash.cpp's between()): x = the first
// block, y = the second, z = the distance past the first centre in half pixels (0 to 128).
uint3 between(uint x, uint blocks) {
    uint halfPixels = 2 * x + 1;
    if (halfPixels < 64) { return uint3(0, 0, 0); }
    uint first = (halfPixels - 64) / 128;
    if (first + 1 >= blocks) { return uint3(blocks - 1, blocks - 1, 0); }
    return uint3(first, first + 1, halfPixels - 64 - first * 128);
}

[numthreads(8, 8, 1)]
void Apply(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) { return; }
    uint3 u = between(id.x, blocksX);
    uint3 v = between(id.y, blocksY);
    uint3 sum = unpack(levels[uint2(u.x, v.x)]) * (128 - u.z) * (128 - v.z) +
                unpack(levels[uint2(u.y, v.x)]) * u.z * (128 - v.z) +
                unpack(levels[uint2(u.x, v.y)]) * (128 - u.z) * v.z +
                unpack(levels[uint2(u.y, v.y)]) * u.z * v.z;
    uint3 wash = (sum + 8192) / 16384;
    uint4 c = bytesAt(id.xy);
    uint3 share = (wash * (255 - c.a) + 127) / 255;
    uint3 rgb = c.rgb - min(share, c.rgb);
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
    std::array<ComPtr<ID3DBlob>, 3> code;
    const std::array<const char*, 3> entries{"BlockFloor", "BlockLevel", "Apply"};
    for (std::size_t i = 0; i < code.size(); ++i) {
        ComPtr<ID3DBlob> errors;
        if (FAILED(compile(kShader, std::strlen(kShader), "ui_wash", nullptr, nullptr, entries[i], "cs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code[i], &errors))) {
            if (errors && errors->GetBufferSize() > 0) {
                EVR_LOG("ui: wash shader %s: %.*s", entries[i], static_cast<int>(errors->GetBufferSize()),
                        static_cast<const char*>(errors->GetBufferPointer()));
            }
            why = "the shader did not compile";
            return false;
        }
    }
    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3;
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
    std::array<ComPtr<ID3D12PipelineState>*, 3> pipelines{&blockPipeline_, &levelPipeline_, &applyPipeline_};
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
    heapDesc.NumDescriptors = 4;
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
    levels_.Reset();
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
                                                IID_PPV_ARGS(&floors_))) ||
        FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&levels_)))) {
        floors_.Reset();
        return false;
    }
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&output_)))) {
        floors_.Reset();
        levels_.Reset();
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
        if (!imagesFailedLogged_) {
            imagesFailedLogged_ = true;
            EVR_LOG("ui: wash filter images (%ux%u) not made; the GUI image is copied unfiltered", width,
                    height);
        }
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
    cpu.ptr += descriptorSize_;
    uav.Format = DXGI_FORMAT_R32_UINT;
    device_->CreateUnorderedAccessView(levels_.Get(), nullptr, &uav, cpu);

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
    list->SetPipelineState(levelPipeline_.Get());
    list->Dispatch((blocksX + 7) / 8, (blocksY + 7) / 8, 1);
    const D3D12_RESOURCE_BARRIER levelsWritten = uavBarrier(levels_.Get());
    list->ResourceBarrier(1, &levelsWritten);
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
    levels_.Reset();
    output_.Reset();
    heap_.Reset();
    blockPipeline_.Reset();
    levelPipeline_.Reset();
    applyPipeline_.Reset();
    rootSignature_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

} // namespace evr::vkcore
