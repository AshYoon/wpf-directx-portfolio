#include <Windows.h>
#include <cwchar>
#include "SceneRenderer.h"

namespace {
struct HostState {
    SR_Handle renderer = nullptr;
    SR_Result failure = 0;
    bool closing = false;
};

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* state = reinterpret_cast<HostState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (state && state->renderer) {
            state->failure = SR_Resize(state->renderer, LOWORD(lParam), HIWORD(lParam));
            if (state->failure < 0) state->closing = true;
        }
        return 0;
    case WM_CLOSE:
        if (state) state->closing = true;
        return 0; // The loop destroys the renderer before its HWND.
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowFailure(SR_Result result) {
    wchar_t text[128]{};
    swprintf_s(text, L"SceneRenderer failed (HRESULT 0x%08X).", static_cast<unsigned int>(result));
    MessageBoxW(nullptr, text, L"Scene Engine", MB_OK | MB_ICONERROR);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    // --smoke exercises the DLL lifecycle without leaving a visible window open.
    const bool smoke = std::wcscmp(commandLine, L"--smoke") == 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"SceneEngine.NativeHost";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass)) return 1;

    HostState state;
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Scene Engine | Direct3D 11 Foundation",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
        nullptr, nullptr, instance, &state);
    if (!window) return 1;

    state.failure = SR_Create(&state.renderer);
    if (state.failure >= 0) state.failure = SR_Initialize(state.renderer, window);
    if (state.failure >= 0 && smoke) {
        state.failure = SR_Render(state.renderer);
        if (state.failure >= 0) state.failure = SR_Resize(state.renderer, 0, 0);
        if (state.failure >= 0 && SR_Render(state.renderer) != 1) state.failure = E_FAIL;
        if (state.failure >= 0) state.failure = SR_Resize(state.renderer, 640, 480);
        if (state.failure >= 0) state.failure = SR_Render(state.renderer);
    } else if (state.failure >= 0) {
        ShowWindow(window, showCommand);
        while (!state.closing && state.failure >= 0) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) { state.closing = true; break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (state.closing || state.failure < 0) break;
            if (IsIconic(window)) { WaitMessage(); continue; }
            state.failure = SR_Render(state.renderer);
            if (state.failure == 1) Sleep(50); // Avoid spinning while occluded.
        }
    }

    SR_Destroy(state.renderer);
    state.renderer = nullptr;
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    if (state.failure < 0 && !smoke) ShowFailure(state.failure);
    return state.failure < 0 ? 1 : 0;
}
