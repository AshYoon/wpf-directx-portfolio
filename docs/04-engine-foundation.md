# Direct3D 11 엔진 기초 골격

작성: 2026-09-06. 이번 범위는 이후 기능을 붙일 수 있는 빌드 가능한 네이티브 기반입니다. 전체 엔진이나 편집기를 완성한 단계가 아닙니다.

## 책임과 호출 흐름

```text
NativeHost (나중에 WPF HwndHost로 교체)
  └─ SceneRenderer.h: extern "C" API
       └─ SceneRenderer.cpp: opaque handle, 호출 스레드 검증
            └─ Renderer: D3D11 device/context, DXGI swap chain, render target
```

기존 ATTS의 HWND 기반 렌더링 연결을 참고했습니다. 새 코드는 기존 엔진을 링크하거나 복사하지 않고 작성했으며, C++ 장식 이름 대신 고정된 C 함수 이름을 내보냅니다. 클래스와 COM 객체는 DLL 안에서만 사용합니다.

## 공개 API v1

| 함수 | 역할 |
| --- | --- |
| SR_GetApiVersion | ABI 버전 1 반환 |
| SR_Create | 아직 초기화하지 않은 핸들 생성 |
| SR_Initialize | 호스트 HWND에 D3D11 장치와 스왑 체인 연결 |
| SR_Resize | 실제 픽셀 단위 크기 반영, 0 크기는 일시 중지 |
| SR_Render | 배경색 지우기와 VSync Present |
| SR_Destroy | GPU 자원과 핸들 해제 |

`SR_Result`는 signed 32-bit HRESULT입니다. 음수는 실패, 0은 성공, 1(S_FALSE)은 크기 0 또는 가려짐으로 렌더가 중지·생략된 상태입니다. HRESULT 실패는 호스트가 처리합니다. 현재 샘플은 실패 시 루프를 종료하고 오류 코드를 안내합니다.

호출 순서는 `Create → Initialize → (Resize / Render 반복) → Destroy → HWND 해제`입니다. 초기화 실패 시 자원을 정리하므로 같은 핸들로 다시 초기화할 수 있습니다. 이미 초기화된 핸들의 재초기화는 실패합니다.

호출자는 유효한 포인터·핸들을 제공해야 합니다. NULL은 검사하지만 임의 주소, 이중 해제, 해제 후 접근은 지원하지 않습니다. `Destroy(NULL)`은 성공합니다.

추후 P/Invoke는 `CallingConvention.Cdecl`, `ExactSpelling = true`를 사용하고, 핸들과 HWND는 `IntPtr`, 결과는 `int`, 크기는 `uint`에 대응시킵니다. 실제 C# 프로젝트는 이번에 만들지 않았습니다.

## 수명과 스레드

현재는 **핸들을 생성한 하나의 스레드에서 모든 API를 호출**합니다. 다른 스레드의 호출은 `RPC_E_WRONG_THREAD`로 거절합니다. 호스트가 HWND와 메시지 루프를 소유하며, 샘플은 같은 스레드에서 렌더링합니다.

렌더 스레드, 명령 큐와 콜백은 아직 없습니다. WPF 연동 시에는 그 소유 스레드에서 생성·초기화·렌더·해제 전체를 수행하고, 렌더 루프 종료가 확인된 후 HWND를 해제하는 구조로 확장합니다.

## GPU 기반

- C++17, Windows x64, Direct3D 11.0 기능 수준.
- 하드웨어 장치 생성 실패 시 WARP 소프트웨어 장치를 시도합니다.
- Debug 구성은 D3D11 debug layer를 요청하고, 해당 구성 요소가 없으면 제외하고 재시도합니다.
- 2개 버퍼의 flip-discard 스왑 체인, VSync 사용.
- 매 프레임 render target을 다시 연결하고 어두운 배경을 지웁니다.
- 리사이즈 전에 back buffer 연결과 뷰 참조를 해제합니다.
- COM 자원은 ComPtr로 소유합니다. 초기화 실패·정상 종료에서 모두 해제합니다.
- Device lost 자동 복구는 아직 없습니다. Present/Resize 실패를 반환하고 호스트가 종료합니다.

구현 근거: Microsoft의 [DXGI flip model 지침](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model), [ResizeBuffers 계약](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

## 확인한 범위

2026-09-06, Windows 10.0.19045, Visual Studio 2022 17.14, MSVC 19.36.32548, Windows SDK 10.0.26100.0에서 확인했습니다. 프리셋은 VS 2022의 기본 설치 도구 체인을 사용하며 컴파일러 패치 버전은 고정하지 않습니다.

| 확인 | 결과 |
| --- | --- |
| CMake 구성 및 x64 Debug·Release 컴파일·링크 | 성공 |
| 각 구성의 NativeHost --smoke | 종료 코드 0 |
| 숨긴 HWND에서 초기화·Render·0 크기 중지·640×480 복원·Render·해제 | 성공 |
| Release DLL의 C export 6개 확인 | 성공 |
| 실제 표시 화면, DPI, 사용자 입력·최소화 반복 | 미검증 |
| WARP 강제 실행, 장치 손실, 메모리·성능 측정 | 미검증 |

`--smoke`는 짧은 실행 점검이며 픽셀 비교 또는 GPU 화면 품질 검증이 아닙니다. Debug 배포에는 개발 도구 런타임이 필요합니다. 다른 PC에 Release를 배포할 때는 해당 MSVC x64 런타임도 준비해야 하며, 깨끗한 환경에서의 배포 검증은 아직 수행하지 않았습니다.

## 다음 구현 지점

| 기능 | 확장 위치 |
| --- | --- |
| 깊이 버퍼, 셰이더, 첫 도형 | Renderer에서 시작하고 커지면 DeviceResources / Mesh / Shader로 분리 |
| 카메라와 장면 모델 | 기능 착수 시 Camera / Scene을 별도 추가 |
| WPF 연결 | src/SceneEditor.Rendering에 HwndHost와 C ABI 어댑터 추가 |
| UI와 저장 | 기존 개발 계획의 App / Core / Infrastructure 단계에서 추가 |

아직 쓰이지 않는 매니저·상속 계층·빈 WPF 프로젝트는 추가하지 않았습니다. 이 골격만으로는 M1의 기본 도형·WPF 연동 완료 기준을 충족하지 않습니다.
