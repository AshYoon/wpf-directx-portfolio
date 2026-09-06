#include "SpatialWorld.h"
#include "SVRThreadPool.h"
#include <chrono>
#include <exception>
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

namespace scene {
// 월드 좌표를 셀 인덱스로 바꾸고 경계 밖 좌표는 첫·마지막 셀로 제한한다.
int SpatialWorld::Cell(float coordinate) noexcept {
    return std::clamp(static_cast<int>(coordinate / CellSize), 0, Cells - 1);
}

// 고정 seed와 분포 설정으로 객체의 초기 위치와 속도를 만든다.
void SpatialWorld::Reset(uint32_t count, uint32_t seed, bool clustered) {
    if (count > 10000) throw std::invalid_argument("At most 10000 particles");
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> position(0.0f, Extent);
    std::uniform_real_distribution<float> velocity(-5.0f, 5.0f);
    std::normal_distribution<float> cluster(50.0f, 7.0f);
    std::vector<Particle> particles;
    particles.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const float x = clustered ? std::clamp(cluster(random), 0.0f, Extent) : position(random);
        const float y = clustered ? std::clamp(cluster(random), 0.0f, Extent) : position(random);
        particles.push_back({x, y, velocity(random), velocity(random)});
    }
    SetParticles(std::move(particles));
}

// 좌표·속도를 검증한 객체 배열로 교체하고 공간 그리드를 다시 만든다.
void SpatialWorld::SetParticles(std::vector<Particle> particles) {
    if (particles.size() > 10000) throw std::invalid_argument("At most 10000 particles");
    for (const auto& p : particles)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.vx) ||
            !std::isfinite(p.vy) || p.x < 0 || p.x > Extent || p.y < 0 || p.y > Extent)
            throw std::invalid_argument("Particle outside finite world");
    particles_ = std::move(particles);
    Rebuild();
}

// 프레임 간 이동 시간을 제한하고 월드 경계에 닿은 객체를 반사시킨다.
void SpatialWorld::Move(float seconds) {
    seconds = std::clamp(seconds, 0.0f, 0.05f);
    for (auto& p : particles_) {
        p.x += p.vx * seconds;
        p.y += p.vy * seconds;
        if (p.x < 0) { p.x = -p.x; p.vx = std::abs(p.vx); }
        if (p.x > Extent) { p.x = Extent * 2 - p.x; p.vx = -std::abs(p.vx); }
        if (p.y < 0) { p.y = -p.y; p.vy = std::abs(p.vy); }
        if (p.y > Extent) { p.y = Extent * 2 - p.y; p.vy = -std::abs(p.vy); }
    }
}

// 모든 객체 ID를 현재 위치의 셀에 다시 등록한다.
void SpatialWorld::Rebuild() {
    for (auto& cell : cells_) cell.clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(particles_.size()); ++i) {
        const auto& p = particles_[i];
        cells_[Cell(p.y) * Cells + Cell(p.x)].push_back(i);
    }
}

namespace {
// 검색 좌표와 반경을 검증하고 이전 검색 결과를 비운다.
void Prepare(float x, float y, float radius, QueryResult& result) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        x < 0 || x > SpatialWorld::Extent || y < 0 || y > SpatialWorld::Extent ||
        radius < 0 || radius > SpatialWorld::Extent * 2)
        throw std::invalid_argument("Invalid query");
    result.hits.clear();
    result.candidates.clear();
}
// 제곱 거리로 원 안에 있는지 검사하며 원 둘레의 점도 포함한다.
bool Inside(const Particle& p, float x, float y, float radius) noexcept {
    const float dx = p.x - x, dy = p.y - y;
    return dx * dx + dy * dy <= radius * radius;
}
}

// 모든 객체를 거리 검사해 기준이 되는 정답과 전체 후보를 만든다.
void SpatialWorld::LinearQuery(float x, float y, float radius, QueryResult& result) const {
    Prepare(x, y, radius, result);
    for (uint32_t i = 0; i < static_cast<uint32_t>(particles_.size()); ++i) {
        result.candidates.push_back(i);
        if (Inside(particles_[i], x, y, radius)) result.hits.push_back(i);
    }
}
// 검색 원의 경계 사각형과 겹치는 셀만 순회한 뒤 거리 검사로 결과를 확정한다.
void SpatialWorld::GridQuery(float x, float y, float radius, QueryResult& result) const {
    Prepare(x, y, radius, result);
    for (int cy = Cell(y - radius); cy <= Cell(y + radius); ++cy)
        for (int cx = Cell(x - radius); cx <= Cell(x + radius); ++cx)
            for (auto id : cells_[cy * Cells + cx]) {
                result.candidates.push_back(id);
                if (Inside(particles_[id], x, y, radius)) result.hits.push_back(id);
            }
}
}

namespace scene {
QueryTimings SpatialWorld::QueryBoth(float x, float y, float radius,
    QueryResult& linear, QueryResult& grid, ThreadPool* workers) const {
    if (&linear == &grid) throw std::invalid_argument("Queries require separate output buffers");
    QueryTimings timings;
    const auto timed = [](auto&& query) {
        const auto begin = std::chrono::steady_clock::now();
        query();
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
    };
    const auto runLinear = [&] { timings.linearMs = timed([&] { LinearQuery(x,y,radius,linear); }); };
    const auto runGrid = [&] { timings.gridMs = timed([&] { GridQuery(x,y,radius,grid); }); };
    // 작은 입력에는 작업 제출 비용을 추가하지 않는다. 이 기준은 성능 보장 값이 아니다.
    if (!workers || particles_.size() < 1000) {
        runLinear();
        runGrid();
        return timings;
    }
    if (workers->IsWorkerThread()) throw std::logic_error("Nested wait on the same pool");
    timings.parallel = true;
    auto first = workers->EnqueueJob(runLinear);
    std::future<void> second;
    try { second = workers->EnqueueJob(runGrid); }
    catch (...) {
        first.wait(); // 두 번째 제출이 실패해도 첫 작업의 참조 수명을 보장한다.
        throw;
    }
    // 한 검색이 실패해도 다른 검색이 끝날 때까지 기다린 뒤 예외를 전달한다.
    std::exception_ptr failure;
    try { first.get(); } catch (...) { failure = std::current_exception(); }
    try { second.get(); } catch (...) { if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
    return timings;
}
}
