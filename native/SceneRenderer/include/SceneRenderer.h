#pragma once
#include <stdint.h>

#ifdef SCENE_RENDERER_EXPORTS
#define SR_API __declspec(dllexport)
#else
#define SR_API __declspec(dllimport)
#endif
#define SR_CALL __cdecl

#ifdef __cplusplus
extern "C" {
#endif
/* ABI v2: Windows x64, cdecl. Negative signed HRESULT = failure.
 * All calls except SR_Enqueue* must use the creating thread, including Destroy.
 * Before Destroy, stop and join every external producer; handles are not lifetime-tracked.
 * Host owns HWND; destroy renderer before its HWND. Never reuse stale handles.
 * No STL, COM, callbacks, or native-owned arrays cross the boundary.
 */
typedef struct SR_RendererOpaque* SR_Handle;
typedef int32_t SR_Result;
// 호출 중 복사하는 입력 설정. size 필드로 양쪽 구조체 크기를 확인한다.
typedef struct SR_Settings {
    uint32_t size;
    uint32_t count;       /* 10..10000 */
    uint32_t seed;
    uint32_t clustered;   /* 0/1 */
    uint32_t use_grid;    /* selects candidate visualization; both queries are timed */
    uint32_t paused;      /* 0/1 */
    uint32_t show_grid;   /* 0/1 */
    float radius;        /* 1..50 world units */
} SR_Settings;
// 프레임 통계 출력. 시간은 밀리초이며 네이티브 포인터는 포함하지 않는다.
typedef struct SR_Stats {
    uint32_t size;
    uint32_t count;
    uint32_t hits;
    uint32_t linear_candidates;
    uint32_t grid_candidates;
    uint32_t results_match;
    uint32_t use_grid;
    uint32_t paused;
    double linear_ms;
    double grid_ms;
    double rebuild_ms;
    double update_ms;
    float query_x;
    float query_y;
} SR_Stats;

// DLL과 호출자의 API 버전 호환성을 확인한다.
SR_API uint32_t SR_CALL SR_GetApiVersion(void);
// 초기화 전 렌더러 핸들을 생성하며 호출 스레드를 소유자로 기록한다.
SR_API SR_Result SR_CALL SR_Create(SR_Handle* out_handle);
// 호스트가 소유한 HWND에 렌더러와 초기 장면을 연결한다.
SR_API SR_Result SR_CALL SR_Initialize(SR_Handle handle, void* hwnd);
// 실제 픽셀 크기를 반영한다. 너비나 높이가 0이면 렌더링을 중지한다.
SR_API SR_Result SR_CALL SR_Resize(SR_Handle handle, uint32_t width, uint32_t height);
// 이동·검색·그리기와 화면 표시를 한 프레임 수행한다.
SR_API SR_Result SR_CALL SR_Render(SR_Handle handle);
// HWND를 파괴하기 전에 생성 스레드에서 렌더러를 해제한다.
SR_API SR_Result SR_CALL SR_Destroy(SR_Handle handle);
/* 설정을 반영한다. reset_scene은 같은 count/seed/distribution의 초기 배치를 재생성한다. */
SR_API SR_Result SR_CALL SR_Configure(SR_Handle handle, const SR_Settings* settings, uint32_t reset_scene);
/* 정규화된 뷰포트 좌표로 검색 중심을 변경하며, 월드 밖의 클릭은 경계로 제한한다. */
SR_API SR_Result SR_CALL SR_SetQuery(SR_Handle handle, float x, float y);
// 마지막 검색 통계를 호출자가 제공한 구조체로 복사한다.
SR_API SR_Result SR_CALL SR_GetStats(SR_Handle handle, SR_Stats* stats);
/* Read-only diagnostic capture: tightly packed RGBA8 at current physical dimensions.
 * Does not advance simulation. Caller owns buffer. Width/height must match viewport.
 */
SR_API SR_Result SR_CALL SR_CopyFrame(SR_Handle handle, uint8_t* rgba, uint32_t width, uint32_t height);

/* ABI v2 compatible additions: 기존 구조체·함수는 변경하지 않는다.
 * Enqueue는 여러 스레드에서 호출할 수 있다. S_OK는 적용 완료가 아닌 큐 접수 성공이다.
 * 입력은 호출 중 복사하며, 초기화 전에는 E_UNEXPECTED, 큐 포화 시에는
 * HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)를 반환한다.
 * Render 시작 또는 소유 스레드의 FlushCommands에서 FIFO로 반영한다.
 * GetStats/CopyFrame은 큐를 비우지 않는다. 동기 API와 섞을 때는 먼저 Flush한다.
 * Destroy 전에 모든 생산자를 중지·join해야 하며, 남은 명령은 Destroy에서 취소한다.
 */
SR_API SR_Result SR_CALL SR_EnqueueConfigure(SR_Handle handle, const SR_Settings* settings, uint32_t reset_scene);
SR_API SR_Result SR_CALL SR_EnqueueQuery(SR_Handle handle, float x, float y);
SR_API SR_Result SR_CALL SR_EnqueueResize(SR_Handle handle, uint32_t width, uint32_t height);
/* 한 배치를 적용한다. 실패한 명령은 소비하고 후속 명령은 다음 호출까지 보관한다. */
SR_API SR_Result SR_CALL SR_FlushCommands(SR_Handle handle);

#ifdef __cplusplus
}
#endif
