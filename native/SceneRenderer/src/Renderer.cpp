#include "Renderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace scene {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
namespace {
double Milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
constexpr uint32_t MaxVertices = 80000;
// 동기 설정과 큐 입력이 같은 값 검증을 사용한다.
bool ValidSettings(const SR_Settings& settings) noexcept {
    return settings.size == sizeof(SR_Settings) && settings.count >= 10 && settings.count <= 10000 &&
        settings.clustered <= 1 && settings.use_grid <= 1 && settings.paused <= 1 && settings.show_grid <= 1 &&
        std::isfinite(settings.radius) && settings.radius >= 1 && settings.radius <= 50;
}
HRESULT QueueResult(EnqueueResult result) noexcept {
    if (result == EnqueueResult::Closed) return E_UNEXPECTED;
    if (result == EnqueueResult::Full) return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    return S_OK;
}
}
Renderer::~Renderer() noexcept { Reset(); }

// GPU 바인딩을 해제하고 장치·스왑 체인·셰이더 자원을 정리한다.
void Renderer::Reset() noexcept {
    commands_.Close();
    processingCommands_.clear();
    nextCommand_ = 0;
    // 월드와 GPU 자원을 해제하기 전에 모든 CPU 작업을 마친다.
    workers_.reset();
    if (context_) { context_->ClearState(); context_->Flush(); }
    rasterizer_.Reset(); vertexBuffer_.Reset(); inputLayout_.Reset();
    pixelShader_.Reset(); vertexShader_.Reset(); renderTarget_.Reset();
    swapChain_.Reset(); context_.Reset(); device_.Reset();
    suspended_ = false;
}
// HWND에 D3D11 장치와 초기 장면을 연결하고, 실패하면 생성한 자원을 정리한다.
HRESULT Renderer::Initialize(HWND window) {
    if (device_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    if (!IsWindow(window)) return E_INVALIDARG;
    const auto initialize = [&]() -> HRESULT {
        constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        auto createDevice = [&](D3D_DRIVER_TYPE driver) {
            context_.Reset(); device_.Reset();
            return D3D11CreateDevice(nullptr, driver, nullptr, flags, levels, 1,
                D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr, context_.GetAddressOf());
        };
        HRESULT hr = createDevice(D3D_DRIVER_TYPE_HARDWARE);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = createDevice(D3D_DRIVER_TYPE_HARDWARE);
        }
        if (FAILED(hr)) {
            hr = createDevice(D3D_DRIVER_TYPE_WARP);
            if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
                flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                hr = createDevice(D3D_DRIVER_TYPE_WARP);
            }
        }
        if (FAILED(hr)) return hr;
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        hr = device_.As(&dxgiDevice);
        if (FAILED(hr)) return hr;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) return hr;
        RECT client{};
        if (!GetClientRect(window, &client)) return HRESULT_FROM_WIN32(GetLastError());
        const LONG width = client.right - client.left, height = client.bottom - client.top;
        suspended_ = width <= 0 || height <= 0;
        width_ = width > 0 ? static_cast<uint32_t>(width) : 1;
        height_ = height > 0 ? static_cast<uint32_t>(height) : 1;
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width_; description.Height = height_;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory->CreateSwapChainForHwnd(device_.Get(), window, &description,
            nullptr, nullptr, swapChain_.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(hr)) return hr;
        hr = CreateRenderTarget();
        if (FAILED(hr)) return hr;
        hr = CreatePipeline();
        if (FAILED(hr)) return hr;
        world_.Reset(settings_.count, settings_.seed, settings_.clustered != 0);
        linear_.hits.reserve(10000); linear_.candidates.reserve(10000);
        grid_.hits.reserve(10000); grid_.candidates.reserve(10000);
        sortedHits_.reserve(10000); marks_.reserve(10000); vertices_.reserve(MaxVertices);
        workers_ = std::make_unique<ThreadPool>(2);
        lastTick_ = Clock::now();
        Update();
        commands_.Open();
        return S_OK;
    };
    HRESULT result = E_FAIL;
    try { result = initialize(); }
    catch (...) { Reset(); throw; }
    if (FAILED(result)) Reset();
    return result;
}
// 스왑 체인의 back buffer를 그리기 대상으로 사용할 뷰를 만든다.
HRESULT Renderer::CreateRenderTarget() noexcept {
    ComPtr<ID3D11Texture2D> buffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return device_->CreateRenderTargetView(buffer.Get(), nullptr, renderTarget_.ReleaseAndGetAddressOf());
}
// 위치와 색상만 사용하는 셰이더, 입력 형식과 동적 정점 버퍼를 만든다.
HRESULT Renderer::CreatePipeline() noexcept {
    constexpr char shader[] =
        "struct V {float4 p:SV_POSITION; float4 c:COLOR;};"
        "V VS(float2 p:POSITION,float4 c:COLOR){V o;o.p=float4(p,0,1);o.c=c;return o;}"
        "float4 PS(V i):SV_TARGET{return i.c;}";
    ComPtr<ID3DBlob> vs, ps, errors;
    HRESULT hr = D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "VS",
        "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) return hr;
    errors.Reset();
    hr = D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "PS",
        "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, ps.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertexShader_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixelShader_.GetAddressOf());
    if (FAILED(hr)) return hr;
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = device_->CreateInputLayout(layout, 2, vs->GetBufferPointer(), vs->GetBufferSize(), inputLayout_.GetAddressOf());
    if (FAILED(hr)) return hr;
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = MaxVertices * static_cast<UINT>(sizeof(Vertex));
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&buffer, nullptr, vertexBuffer_.GetAddressOf());
    if (FAILED(hr)) return hr;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    return device_->CreateRasterizerState(&rasterizer, rasterizer_.GetAddressOf());
}
// 실제 픽셀 크기로 back buffer를 재생성하고, 크기 0에서는 렌더링을 쉰다.
HRESULT Renderer::Resize(uint32_t width, uint32_t height) noexcept {
    if (!swapChain_) return E_UNEXPECTED;
    if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return E_INVALIDARG;
    suspended_ = width == 0 || height == 0;
    lastTick_ = Clock::now();
    if (suspended_) return S_FALSE;
    if (width == width_ && height == height_ && renderTarget_) return S_OK;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    renderTarget_.Reset();
    const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return hr;
    width_ = width; height_ = height;
    return CreateRenderTarget();
}
// 설정을 검증하고 객체 수·seed·분포가 바뀌었을 때 장면을 재생성한다.
HRESULT Renderer::Configure(const SR_Settings& settings, bool reset) {
    if (!ValidSettings(settings)) return E_INVALIDARG;
    if (!device_) return E_UNEXPECTED;
    if (reset || settings.count != settings_.count || settings.seed != settings_.seed ||
        settings.clustered != settings_.clustered) {
        world_.Reset(settings.count, settings.seed, settings.clustered != 0);
        queryX_ = queryY_ = 50;
    }
    settings_ = settings;
    lastTick_ = Clock::now();
    Update(false);
    return S_OK;
}
// 정규화된 화면 좌표를 월드 좌표로 바꾸고 다음 그리기 전에 검색하도록 표시한다.
HRESULT Renderer::SetQuery(float x, float y) noexcept {
    if (!device_) return E_UNEXPECTED;
    if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
    const float minimum = static_cast<float>(std::min(width_, height_));
    const float sx = minimum / static_cast<float>(width_) * 0.9f;
    const float sy = minimum / static_cast<float>(height_) * 0.9f;
    queryX_ = std::clamp(((x * 2 - 1) / sx + 1) * 50, 0.0f, 100.0f);
    queryY_ = std::clamp(((1 - y * 2) / sy + 1) * 50, 0.0f, 100.0f);
    queryDirty_ = true;
    return S_OK;
}
// 호출자 구조체에 가장 최근 프레임의 검색·이동 통계를 복사한다.
HRESULT Renderer::GetStats(SR_Stats& stats) const noexcept {
    if (stats.size != sizeof(SR_Stats)) return E_INVALIDARG;
    if (!device_) return E_UNEXPECTED;
    stats = stats_;
    return S_OK;
}
// 이동과 그리드 갱신 후 두 검색을 실행하고 정답 일치 여부와 CPU 시간을 기록한다.
void Renderer::Update(bool advance) {
    const auto now = Clock::now();
    const float delta = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    auto start = Clock::now();
    if (advance && !settings_.paused) world_.Move(delta);
    auto moved = Clock::now();
    if (advance && !settings_.paused) world_.Rebuild();
    auto rebuilt = Clock::now();
    // 두 검색은 서로 다른 결과 버퍼를 쓰며, 완료 전에는 world_를 수정하지 않는다.
    const auto queryTimes = world_.QueryBoth(queryX_, queryY_, settings_.radius, linear_, grid_, workers_.get());
    // 그리드는 셀 순서로 결과를 반환하므로 ID를 정렬해서 전체 순회 결과와 비교한다.
    sortedHits_ = grid_.hits;
    std::sort(sortedHits_.begin(), sortedHits_.end());
    stats_ = {};
    stats_.size = sizeof(SR_Stats);
    stats_.count = static_cast<uint32_t>(world_.Particles().size());
    stats_.hits = static_cast<uint32_t>(linear_.hits.size());
    stats_.linear_candidates = static_cast<uint32_t>(linear_.candidates.size());
    stats_.grid_candidates = static_cast<uint32_t>(grid_.candidates.size());
    stats_.results_match = sortedHits_ == linear_.hits ? 1u : 0u;
    stats_.use_grid = settings_.use_grid; stats_.paused = settings_.paused;
    stats_.linear_ms = queryTimes.linearMs;
    stats_.grid_ms = queryTimes.gridMs;
    stats_.rebuild_ms = (!advance || settings_.paused) ? 0 : Milliseconds(moved, rebuilt);
    stats_.update_ms = (!advance || settings_.paused) ? 0 : Milliseconds(start, moved);
    stats_.query_x = queryX_; stats_.query_y = queryY_;
    queryDirty_ = false;
}
// 객체·검색 원·그리드를 정점으로 만들어 삼각형과 선으로 그린다.
HRESULT Renderer::DrawScene() {
    if (!renderTarget_) return E_UNEXPECTED;
    if (queryDirty_) Update(false);
    vertices_.clear();
    // 가로세로 비율에 관계없이 월드가 정사각형으로 보이도록 같은 축척을 사용한다.
    const float minimum = static_cast<float>(std::min(width_, height_));
    const float sx = minimum / static_cast<float>(width_) * 0.9f;
    const float sy = minimum / static_cast<float>(height_) * 0.9f;
    auto point = [&](float x, float y, float r, float g, float b) {
        vertices_.push_back({(x / 50 - 1) * sx, (y / 50 - 1) * sy, r, g, b, 1});
    };
    auto quad = [&](float x, float y, float w, float h, float r, float g, float b) {
        point(x,y,r,g,b); point(x+w,y,r,g,b); point(x+w,y+h,r,g,b);
        point(x,y,r,g,b); point(x+w,y+h,r,g,b); point(x,y+h,r,g,b);
    };
    quad(0, 0, 100, 100, 0.047f, 0.084f, 0.125f);
    if (settings_.show_grid && settings_.use_grid)
        for (int y = SpatialWorld::Cell(queryY_ - settings_.radius); y <= SpatialWorld::Cell(queryY_ + settings_.radius); ++y)
            for (int x = SpatialWorld::Cell(queryX_ - settings_.radius); x <= SpatialWorld::Cell(queryX_ + settings_.radius); ++x)
                quad(x*5.0f, y*5.0f, 5, 5, 0.074f, 0.16f, 0.19f);
    marks_.assign(world_.Particles().size(), 0);
    const auto& query = settings_.use_grid ? grid_ : linear_;
    for (auto id : query.candidates) marks_[id] = 1;
    for (auto id : query.hits) marks_[id] = 2;
    const float halfSize = settings_.count >= 5000 ? 0.19f : 0.32f;
    for (size_t i = 0; i < world_.Particles().size(); ++i) {
        const auto& p = world_.Particles()[i];
        const auto m = marks_[i];
        const float r = m == 2 ? 0.29f : m == 1 ? 0.94f : 0.28f;
        const float g = m == 2 ? 0.93f : m == 1 ? 0.66f : 0.40f;
        const float b = m == 2 ? 0.76f : m == 1 ? 0.32f : 0.52f;
        quad(p.x-halfSize, p.y-halfSize, halfSize*2, halfSize*2, r, g, b);
    }
    const UINT triangles = static_cast<UINT>(vertices_.size());
    if (settings_.show_grid) {
        for (int cell = 0; cell <= SpatialWorld::Cells; ++cell) {
            float v = cell * SpatialWorld::CellSize;
            point(v,0,0.12f,0.21f,0.28f); point(v,100,0.12f,0.21f,0.28f);
            point(0,v,0.12f,0.21f,0.28f); point(100,v,0.12f,0.21f,0.28f);
        }
    }
    for (int i = 0; i < 128; ++i) {
        float a = i * 6.2831853f / 128, b = (i+1) * 6.2831853f / 128;
        point(queryX_ + std::cos(a)*settings_.radius, queryY_ + std::sin(a)*settings_.radius, 0.36f, 1.0f, 0.85f);
        point(queryX_ + std::cos(b)*settings_.radius, queryY_ + std::sin(b)*settings_.radius, 0.36f, 1.0f, 0.85f);
    }
    point(queryX_-1,queryY_,1,1,1); point(queryX_+1,queryY_,1,1,1);
    point(queryX_,queryY_-1,1,1,1); point(queryX_,queryY_+1,1,1,1);
    if (vertices_.size() > MaxVertices) return E_UNEXPECTED;
    // 이번 프레임의 정점만 GPU 버퍼에 쓰고 다시 그리기 파이프라인에 연결한다.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;
    std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
    context_->Unmap(vertexBuffer_.Get(), 0);
    ID3D11RenderTargetView* target = renderTarget_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    constexpr float background[] = {0.027f, 0.047f, 0.074f, 1};
    context_->ClearRenderTargetView(target, background);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(rasterizer_.Get());
    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* buffer = vertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context_->IASetInputLayout(inputLayout_.Get());
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->Draw(triangles, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context_->Draw(static_cast<UINT>(vertices_.size()) - triangles, triangles);
    return S_OK;
}
// 장면을 한 프레임 갱신하고 그린 뒤 VSync로 화면에 표시한다.
HRESULT Renderer::Render() {
    if (!swapChain_) return E_UNEXPECTED;
    // 최소화 중에도 복원용 리사이즈 명령을 먼저 적용한다.
    HRESULT hr = FlushCommands(false);
    if (FAILED(hr)) return hr;
    if (suspended_) {
        if (queryDirty_) Update(false);
        lastTick_ = Clock::now();
        return S_FALSE;
    }
    Update();
    hr = DrawScene();
    if (FAILED(hr)) return hr;
    hr = swapChain_->Present(1, 0);
    return hr == DXGI_STATUS_OCCLUDED ? S_FALSE : hr;
}
// 시뮬레이션을 이동시키지 않고 현재 장면의 RGBA 픽셀을 호출자 버퍼에 복사한다.
HRESULT Renderer::CopyFrame(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba) return E_POINTER;
    if (width != width_ || height != height_ || suspended_) return E_INVALIDARG;
    if (!swapChain_) return E_UNEXPECTED;
    HRESULT hr = DrawScene();
    if (FAILED(hr)) return hr;
    ComPtr<ID3D11Texture2D> buffer, staging;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC description{};
    buffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    hr = device_->CreateTexture2D(&description, nullptr, staging.GetAddressOf());
    if (FAILED(hr)) return hr;
    context_->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    // GPU 행 간격(RowPitch)에 포함된 패딩을 제외하고 픽셀만 연속으로 복사한다.
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(rgba + static_cast<size_t>(row)*width*4,
            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(row)*mapped.RowPitch,
            static_cast<size_t>(width)*4);
    context_->Unmap(staging.Get(), 0);
    return S_OK;
}
}

namespace scene {
// 생산자는 큐만 접근한다. 장면·GPU 자원은 여기서 변경하지 않는다.
HRESULT Renderer::EnqueueConfigure(const SR_Settings& settings, bool reset) {
    if (!ValidSettings(settings)) return E_INVALIDARG;
    return QueueResult(commands_.EnqueueCommand(ConfigureCommand{settings,reset}));
}
HRESULT Renderer::EnqueueQuery(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
    return QueueResult(commands_.EnqueueCommand(QueryCommand{x,y}));
}
HRESULT Renderer::EnqueueResize(uint32_t width, uint32_t height) {
    if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return E_INVALIDARG;
    return QueueResult(commands_.EnqueueCommand(ResizeCommand{width,height}));
}

// 한 번 인출한 명령을 FIFO로 처리한다. 처리 중 새로 들어온 명령은 다음 배치로 넘긴다.
HRESULT Renderer::FlushCommands(bool refreshStatistics) {
    if (!device_) return E_UNEXPECTED;
    if (nextCommand_ == processingCommands_.size()) {
        commands_.FlushToProcessing(processingCommands_);
        nextCommand_ = 0;
    }
    while (nextCommand_ < processingCommands_.size()) {
        // 실패한 명령은 소비하지만 뒤의 명령은 다음 Flush에서 계속 처리한다.
        const auto& command = processingCommands_[nextCommand_++];
        const HRESULT hr = std::visit([this](const auto& value) -> HRESULT {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T,ConfigureCommand>) return Configure(value.settings,value.reset);
            else if constexpr (std::is_same_v<T,QueryCommand>) return SetQuery(value.x,value.y);
            else return Resize(value.width,value.height);
        }, command);
        if (FAILED(hr)) return hr;
    }
    processingCommands_.clear();
    nextCommand_ = 0;
    if (refreshStatistics && queryDirty_) Update(false);
    return S_OK;
}
}
