#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace scene {

// ATTS의 worker/작업 큐 구조를 분리한 CPU 작업 풀. GPU 자원에는 접근하지 않는다.
class ThreadPool final {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 작업의 완료·예외를 future로 돌려준다. 종료가 시작되면 새 작업을 거절한다.
    std::future<void> EnqueueJob(std::function<void()> job);
    // 접수를 중지하고 이미 받은 작업을 모두 마친 뒤 worker를 join한다.
    // 자신의 worker에서는 호출할 수 없으며, worker에서 풀을 파괴해서도 안 된다.
    void Shutdown();
    bool IsWorkerThread() const noexcept;
    size_t GetPendingCount() const;
    size_t WorkerCount() const noexcept { return workers_.size(); }

private:
    void WorkerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::mutex shutdownMutex_;
    bool stopping_ = false; // 대기 조건과 같은 mutex로 읽기·쓰기를 보호한다.
};
}
