#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include <vector>
#include "SceneRenderer.h"
#include "SpatialWorld.h"
#include "SVRThreadPool.h"
#include "ObjectCommandQueue.h"
#include <memory>

namespace scene {
// 장면 갱신, 공간 검색 통계와 Direct3D 11 그리기 자원을 관리한다.
class Renderer final {
public:
    Renderer() noexcept = default;
    ~Renderer() noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    HRESULT Initialize(HWND window);
    HRESULT Resize(uint32_t width, uint32_t height) noexcept;
    HRESULT Render();
    HRESULT Configure(const SR_Settings& settings, bool reset);
    HRESULT SetQuery(float x, float y) noexcept;
    HRESULT GetStats(SR_Stats& stats) const noexcept;
    HRESULT CopyFrame(uint8_t* rgba, uint32_t width, uint32_t height);
    // Enqueue 계열만 외부 생산자 스레드에서 호출할 수 있다.
    HRESULT EnqueueConfigure(const SR_Settings& settings, bool reset);
    HRESULT EnqueueQuery(float x, float y);
    HRESULT EnqueueResize(uint32_t width, uint32_t height);
    HRESULT FlushCommands(bool refreshStatistics = true);
    bool IsOwnerThread() const noexcept { return ownerThread_ == GetCurrentThreadId(); }
private:
    struct Vertex { float x, y, r, g, b, a; };
    HRESULT CreateRenderTarget() noexcept;
    HRESULT CreatePipeline() noexcept;
    HRESULT DrawScene();
    void Update(bool advance = true);
    void Reset() noexcept;

    // 이 스레드에서만 장면과 GPU 자원을 읽고 변경한다.
    const DWORD ownerThread_ = GetCurrentThreadId();
    CObjectCommandQueue commands_;
    std::vector<ObjectCommand> processingCommands_;
    size_t nextCommand_ = 0;
    // worker는 공간 검색만 읽는다. QueryBoth가 완료를 보장한 후 월드를 변경한다.
    std::unique_ptr<ThreadPool> workers_;
    bool suspended_ = false;
    bool queryDirty_ = true;
    uint32_t width_ = 1, height_ = 1;
    // 검색 대상과 설정, 두 검색의 결과를 프레임 사이에 보관한다.
    SpatialWorld world_;
    SR_Settings settings_{sizeof(SR_Settings), 1000, 42, 0, 1, 0, 1, 12.0f};
    SR_Stats stats_{};
    float queryX_ = 50, queryY_ = 50;
    QueryResult linear_, grid_;
    std::vector<uint32_t> sortedHits_;
    std::vector<uint8_t> marks_;
    std::vector<Vertex> vertices_;
    std::chrono::steady_clock::time_point lastTick_{};
    // COM 자원은 참조 수로 소유하며 Reset에서 사용 순서의 역순으로 해제한다.
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
};
}
