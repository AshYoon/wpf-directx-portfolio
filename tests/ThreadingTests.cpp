#include "ObjectCommandQueue.h"
#include "SVRThreadPool.h"
#include "SpatialWorld.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class E, class F> void Rejects(F&& operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const E&) { rejected = true; }
    Require(rejected,message);
}

// 두 worker가 실제로 동시에 작업하고 예외 후에도 계속 작업할 수 있는지 확인한다.
void PoolLifecycle() {
    Rejects<std::invalid_argument>([] { scene::ThreadPool pool(0); }, "zero workers accepted");
    scene::ThreadPool pool(2);
    std::promise<void> release;
    const auto gate = release.get_future().share();
    std::array<std::promise<void>,2> entered;
    auto firstEntered = entered[0].get_future(), secondEntered = entered[1].get_future();
    std::array<std::thread::id,2> ids{};
    auto first = pool.EnqueueJob([&] { ids[0]=std::this_thread::get_id(); entered[0].set_value(); gate.wait(); });
    auto second = pool.EnqueueJob([&] { ids[1]=std::this_thread::get_id(); entered[1].set_value(); gate.wait(); });
    bool bothEntered = firstEntered.wait_for(2s)==std::future_status::ready &&
        secondEntered.wait_for(2s)==std::future_status::ready;
    release.set_value(); // 검증 실패 경로에서도 worker를 먼저 해제한다.
    first.get(); second.get();
    Require(bothEntered && ids[0]!=ids[1] && ids[0]!=std::this_thread::get_id(),
        "jobs did not execute on two concurrent workers");

    auto failure = pool.EnqueueJob([] { throw std::runtime_error("job failure"); });
    Rejects<std::runtime_error>([&] { failure.get(); }, "job exception not propagated");
    Rejects<std::invalid_argument>([&] { pool.EnqueueJob({}); }, "empty job accepted");
    auto selfStop = pool.EnqueueJob([&] { pool.Shutdown(); });
    Rejects<std::logic_error>([&] { selfStop.get(); }, "worker joined its own pool");
    std::atomic<int> completed{0};
    std::vector<std::future<void>> completions;
    for (int i=0;i<200;++i) completions.push_back(pool.EnqueueJob([&] { ++completed; }));
    pool.Shutdown();
    for (auto& completion : completions) completion.get();
    Require(completed==200 && pool.GetPendingCount()==0, "shutdown lost accepted jobs");
    pool.Shutdown();
    Rejects<std::logic_error>([&] { pool.EnqueueJob([] {}); }, "job accepted after shutdown");
    for (int i=0;i<20;++i) { scene::ThreadPool idle(2); } // 빈 대기 큐에서도 종료 알림을 놓치지 않는다.
}

// 제출과 종료가 겹쳐도 접수된 작업은 모두 완료되고 이후 제출은 거절된다.
void ConcurrentPoolShutdown() {
    scene::ThreadPool pool(2);
    std::atomic<int> accepted{0}, completed{0};
    std::atomic<bool> rejected{false};
    std::vector<std::future<void>> futures;
    std::thread producer([&] {
        for (int i=0;i<2000;++i) {
            try {
                futures.push_back(pool.EnqueueJob([&] { ++completed; }));
                ++accepted;
            } catch (const std::logic_error&) { rejected=true; break; }
        }
    });
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while (accepted<20 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    pool.Shutdown();
    producer.join();
    for (auto& future : futures) future.get();
    Require(completed==accepted, "concurrent shutdown lost accepted work");
    Require(rejected || accepted==2000, "producer did not complete");
}

// 여러 생산자의 명령을 인출 중에도 누락·중복 없이 각 생산자 순서대로 유지한다.
void QueueConcurrency() {
    scene::CObjectCommandQueue queue(2048);
    Require(queue.EnqueueCommand(scene::QueryCommand{0,0})==scene::EnqueueResult::Closed,
        "queue accepted before open");
    queue.Open();
    std::array<std::thread,4> producers;
    std::atomic<int> done{0};
    std::atomic<bool> accepted{true};
    for (int p=0;p<4;++p) producers[p]=std::thread([&,p] {
        for (int i=0;i<256;++i)
            if (queue.EnqueueCommand(scene::QueryCommand{static_cast<float>(i),static_cast<float>(p)})
                !=scene::EnqueueResult::Accepted) accepted=false;
        ++done;
    });
    std::vector<scene::ObjectCommand> batch, received;
    while (done<4 || queue.GetPendingCount()!=0) {
        queue.FlushToProcessing(batch);
        received.insert(received.end(),batch.begin(),batch.end());
        std::this_thread::yield();
    }
    for (auto& producer : producers) producer.join();
    Require(accepted && received.size()==1024, "commands lost or duplicated");
    std::array<int,4> next{};
    for (const auto& command : received) {
        const auto& query=std::get<scene::QueryCommand>(command);
        const auto producer=static_cast<size_t>(query.y);
        Require(producer<next.size() && static_cast<int>(query.x)==next[producer]++,
            "per-producer FIFO order changed");
    }
    queue.Close();
    Require(queue.EnqueueCommand(scene::QueryCommand{0,0})==scene::EnqueueResult::Closed,
        "queue accepted after close");

    scene::CObjectCommandQueue bounded(2);
    bounded.Open();
    SR_Settings settings{sizeof(SR_Settings),1000,42,0,1,1,1,12};
    Require(bounded.EnqueueCommand(scene::ConfigureCommand{settings,true})==scene::EnqueueResult::Accepted,
        "configure enqueue");
    settings.count=10;
    bounded.EnqueueCommand(scene::QueryCommand{0.5f,0.5f});
    Require(bounded.EnqueueCommand(scene::ResizeCommand{640,480})==scene::EnqueueResult::Full,
        "capacity exceeded");
    bounded.FlushToProcessing(batch);
    Require(std::get<scene::ConfigureCommand>(batch[0]).settings.count==1000, "command kept producer reference");
    Require(bounded.EnqueueCommand(scene::ResizeCommand{640,480})==scene::EnqueueResult::Accepted,
        "capacity did not recover after flush");
    bounded.Close();
    Require(bounded.GetPendingCount()==0, "close did not discard pending commands");
    bounded.Open();
    bounded.FlushToProcessing(batch);
    Require(batch.empty(), "reopen resurrected cancelled commands");
}

// 실제 검색 결과를 단일 실행과 비교하고 프레임 갱신·실패 후 재사용을 검증한다.
void ParallelQueries() {
    scene::ThreadPool pool(2);
    scene::SpatialWorld world;
    for (bool clustered : {false,true}) for (uint32_t count : {0u,10u,999u,1000u,10000u}) {
        world.Reset(count,42,clustered);
        for (int step=0;step<6;++step) {
            world.Move(0.02f); world.Rebuild();
            const float center=step%2==0 ? 0.0f : 100.0f;
            const float radius=step%3==0 ? 0.0f : step%3==1 ? 12.0f : 200.0f;
            scene::QueryResult referenceLinear, referenceGrid, linear, grid;
            world.QueryBoth(center,center,radius,referenceLinear,referenceGrid);
            auto timing=world.QueryBoth(center,center,radius,linear,grid,&pool);
            Require(timing.parallel==(count>=1000), "unexpected execution mode");
            Require(timing.linearMs>=0 && timing.gridMs>=0, "invalid query timings");
            Require(linear.hits==referenceLinear.hits && linear.candidates==referenceLinear.candidates &&
                grid.hits==referenceGrid.hits && grid.candidates==referenceGrid.candidates,
                "parallel search differs from sequential search");
        }
    }
    world.Reset(1000,42,false);
    scene::QueryResult linear, grid;
    Rejects<std::invalid_argument>([&] {
        world.QueryBoth(std::numeric_limits<float>::quiet_NaN(),50,12,linear,grid,&pool);
    }, "invalid parallel query accepted");
    Require(world.QueryBoth(50,50,12,linear,grid,&pool).parallel, "pool unusable after query exception");
    Rejects<std::invalid_argument>([&] { world.QueryBoth(50,50,12,linear,linear,&pool); },
        "aliased result buffers accepted");
    auto nested=pool.EnqueueJob([&] { world.QueryBoth(50,50,12,linear,grid,&pool); });
    Rejects<std::logic_error>([&] { nested.get(); }, "nested pool wait accepted");
    pool.Shutdown();
    Rejects<std::logic_error>([&] { world.QueryBoth(50,50,12,linear,grid,&pool); },
        "stopped query pool accepted jobs");
}
}

int main() {
    try {
        PoolLifecycle();
        ConcurrentPoolShutdown();
        QueueConcurrency();
        ParallelQueries();
        std::cout<<"PASS: concurrent workers, exceptions, shutdown, command FIFO/capacity and parallel query equivalence\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
