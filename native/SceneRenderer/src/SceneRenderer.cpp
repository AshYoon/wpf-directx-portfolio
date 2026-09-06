#include "SceneRenderer.h"
#include "Renderer.h"
#include <new>
#include <stdexcept>

// C#과 C++의 구조체 크기가 달라지는 ABI 변경을 빌드 단계에서 확인한다.
static_assert(sizeof(SR_Settings) == 32);
static_assert(sizeof(SR_Stats) == 72);
// 외부에는 내부 C++ 클래스 대신 불투명한 핸들만 공개한다.
struct SR_RendererOpaque { scene::Renderer renderer; };
namespace {
// 핸들과 호출 스레드를 검사하고 C++ 예외를 HRESULT로 변환한다.
template<class F> SR_Result Invoke(SR_Handle handle, F&& function, bool ownerOnly = true) noexcept {
    if (!handle) return E_POINTER;
    if (ownerOnly && !handle->renderer.IsOwnerThread()) return RPC_E_WRONG_THREAD;
    try { return function(handle->renderer); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return E_INVALIDARG; }
    catch (...) { return E_FAIL; }
}
}
uint32_t SR_CALL SR_GetApiVersion(void) { return 2; }
SR_Result SR_CALL SR_Create(SR_Handle* output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    try { *output = new SR_RendererOpaque; return S_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
SR_Result SR_CALL SR_Initialize(SR_Handle h, void* window) {
    return Invoke(h, [&](auto& r) { return r.Initialize(static_cast<HWND>(window)); });
}
SR_Result SR_CALL SR_Resize(SR_Handle h, uint32_t w, uint32_t height) {
    return Invoke(h, [&](auto& r) { return r.Resize(w,height); });
}
SR_Result SR_CALL SR_Render(SR_Handle h) {
    return Invoke(h, [](auto& r) { return r.Render(); });
}
SR_Result SR_CALL SR_Destroy(SR_Handle h) {
    if (!h) return S_OK;
    if (!h->renderer.IsOwnerThread()) return RPC_E_WRONG_THREAD;
    delete h;
    return S_OK;
}
SR_Result SR_CALL SR_Configure(SR_Handle h, const SR_Settings* settings, uint32_t reset) {
    if (!settings) return E_POINTER;
    if (reset > 1) return E_INVALIDARG;
    return Invoke(h, [&](auto& r) { return r.Configure(*settings, reset != 0); });
}
SR_Result SR_CALL SR_SetQuery(SR_Handle h, float x, float y) {
    return Invoke(h, [&](auto& r) { return r.SetQuery(x,y); });
}
SR_Result SR_CALL SR_GetStats(SR_Handle h, SR_Stats* stats) {
    if (!stats) return E_POINTER;
    return Invoke(h, [&](auto& r) { return r.GetStats(*stats); });
}
SR_Result SR_CALL SR_CopyFrame(SR_Handle h, uint8_t* rgba, uint32_t w, uint32_t height) {
    return Invoke(h, [&](auto& r) { return r.CopyFrame(rgba,w,height); });
}


// Enqueue는 입력을 복사하는 큐 접근만 허용한다. 나머지 API의 소유 스레드 검사는 유지한다.
SR_Result SR_CALL SR_EnqueueConfigure(SR_Handle h, const SR_Settings* settings, uint32_t reset) {
    if (!settings) return E_POINTER;
    if (reset > 1) return E_INVALIDARG;
    return Invoke(h, [&](auto& r) { return r.EnqueueConfigure(*settings,reset != 0); }, false);
}
SR_Result SR_CALL SR_EnqueueQuery(SR_Handle h, float x, float y) {
    return Invoke(h, [&](auto& r) { return r.EnqueueQuery(x,y); }, false);
}
SR_Result SR_CALL SR_EnqueueResize(SR_Handle h, uint32_t width, uint32_t height) {
    return Invoke(h, [&](auto& r) { return r.EnqueueResize(width,height); }, false);
}
SR_Result SR_CALL SR_FlushCommands(SR_Handle h) {
    return Invoke(h, [](auto& r) { return r.FlushCommands(); });
}
