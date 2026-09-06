namespace SpatialLab.Models;
// 화면 표시용 통계 스냅샷. 원본 프레임이 바뀌어도 전달한 값은 유지된다.
public sealed record SceneStatistics(uint ObjectCount, uint Hits, uint LinearCandidates,
    uint GridCandidates, bool ResultsMatch, bool UsesGrid, bool IsPaused,
    double LinearMs, double GridMs, double RebuildMs, double UpdateMs, float QueryX, float QueryY);
