#pragma once
#include <cstdint>
#include <vector>

namespace scene {
class ThreadPool;
// 검색 함수 자체의 실행 시간. 큐 대기·join 시간은 포함하지 않는다.
struct QueryTimings { double linearMs = 0, gridMs = 0; bool parallel = false; };
// 평면 객체의 위치와 초당 이동 속도.
struct Particle { float x, y, vx, vy; };
// 실제 검색 결과와 거리 검사를 수행한 후보의 객체 ID를 따로 보관한다.
struct QueryResult {
    std::vector<uint32_t> hits;
    std::vector<uint32_t> candidates;
};

// 렌더러에 의존하지 않고 점 객체의 이동과 원 범위 검색을 처리한다.
class SpatialWorld {
public:
    static constexpr float Extent = 100.0f;
    static constexpr float CellSize = 5.0f;
    static constexpr int Cells = 20;
    void Reset(uint32_t count, uint32_t seed, bool clustered);
    void SetParticles(std::vector<Particle> particles);
    void Move(float seconds);
    void Rebuild();
    void LinearQuery(float x, float y, float radius, QueryResult& result) const;
    void GridQuery(float x, float y, float radius, QueryResult& result) const;
    // 같은 읽기 전용 월드에서 두 검색을 수행하고 모든 작업 완료 후 반환한다.
    QueryTimings QueryBoth(float x, float y, float radius, QueryResult& linear,
        QueryResult& grid, ThreadPool* workers = nullptr) const;
    const std::vector<Particle>& Particles() const noexcept { return particles_; }
    static int Cell(float coordinate) noexcept;
private:
    std::vector<Particle> particles_;
    // 각 셀은 particles_의 인덱스를 보관하며, 이동 후 Rebuild로 갱신한다.
    std::vector<uint32_t> cells_[Cells * Cells];
};
}
