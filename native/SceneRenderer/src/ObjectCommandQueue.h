#pragma once
#include "SceneRenderer.h"
#include <cstddef>
#include <mutex>
#include <variant>
#include <vector>

namespace scene {

// 포인터를 보관하지 않고 생산자가 제출한 명령 데이터를 값으로 복사한다.
struct ConfigureCommand { SR_Settings settings; bool reset; };
struct QueryCommand { float x, y; };
struct ResizeCommand { uint32_t width, height; };
using ObjectCommand = std::variant<ConfigureCommand, QueryCommand, ResizeCommand>;

enum class EnqueueResult { Accepted, Closed, Full };

// 여러 생산자가 넣고 하나의 소유 스레드가 처리하는 ATTS의 swap 방식 명령 큐.
class CObjectCommandQueue final {
public:
    explicit CObjectCommandQueue(size_t capacity = 4096);
    // 초기화가 끝난 뒤 접수를 연다. 이미 열렸다면 기존 명령을 유지한다.
    void Open();
    EnqueueResult EnqueueCommand(const ObjectCommand& command);
    // 잠금 안에서는 버퍼만 교환한다. 명령 실행은 호출자가 잠금 밖에서 수행한다.
    void FlushToProcessing(std::vector<ObjectCommand>& output);
    // 새 요청을 거절하고 아직 인출하지 않은 명령을 취소한다.
    void Close();
    size_t GetPendingCount() const;

private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<ObjectCommand> pending_;
    bool open_ = false;
};
}
