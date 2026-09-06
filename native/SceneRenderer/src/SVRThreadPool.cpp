#include "SVRThreadPool.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace scene {
namespace {
thread_local const ThreadPool* activePool = nullptr;
}

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) throw std::invalid_argument("ThreadPool requires at least one worker");
    workers_.reserve(numThreads);
    try {
        for (size_t i = 0; i < numThreads; ++i)
            workers_.emplace_back([this] { WorkerLoop(); });
    } catch (...) {
        // 일부 스레드만 생성된 경우에도 join 가능한 스레드를 남기지 않는다.
        Shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() { Shutdown(); }

std::future<void> ThreadPool::EnqueueJob(std::function<void()> job) {
    if (!job) throw std::invalid_argument("Empty job");
    // packaged_task가 사용자 작업의 예외를 보관하므로 worker가 예외로 종료되지 않는다.
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(job));
    auto completion = task->get_future();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (stopping_) throw std::logic_error("ThreadPool is stopping");
        jobs_.emplace([task] { (*task)(); });
    }
    condition_.notify_one();
    return completion;
}

void ThreadPool::Shutdown() {
    if (IsWorkerThread()) throw std::logic_error("A worker cannot join its own pool");
    // 여러 외부 호출자가 동시에 종료해도 같은 worker를 중복 join하지 않는다.
    std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

bool ThreadPool::IsWorkerThread() const noexcept { return activePool == this; }

size_t ThreadPool::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return jobs_.size();
}

void ThreadPool::WorkerLoop() {
    activePool = this;
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) break;
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        // 실행 중에는 큐 잠금을 풀어 다른 worker와 생산자가 진행할 수 있게 한다.
        job();
    }
    activePool = nullptr;
}
}
