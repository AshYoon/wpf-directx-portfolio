#include "ObjectCommandQueue.h"
#include <stdexcept>

namespace scene {
CObjectCommandQueue::CObjectCommandQueue(size_t capacity) : capacity_(capacity) {
    if (capacity == 0) throw std::invalid_argument("Command capacity must be positive");
}

void CObjectCommandQueue::Open() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
}

EnqueueResult CObjectCommandQueue::EnqueueCommand(const ObjectCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return EnqueueResult::Closed;
    if (pending_.size() >= capacity_) return EnqueueResult::Full;
    pending_.push_back(command);
    return EnqueueResult::Accepted;
}

void CObjectCommandQueue::FlushToProcessing(std::vector<ObjectCommand>& output) {
    output.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    output.swap(pending_);
}

void CObjectCommandQueue::Close() {
    std::vector<ObjectCommand> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
        cancelled.swap(pending_);
    }
    // 취소한 명령의 저장 공간은 잠금 밖에서 해제한다.
}

size_t CObjectCommandQueue::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}
}
