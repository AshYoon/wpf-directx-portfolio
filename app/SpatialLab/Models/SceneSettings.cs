using SpatialLab.GlobalEnums;
namespace SpatialLab.Models;
// WPF 컨트롤과 네이티브 ABI에 의존하지 않는 장면 설정.
public sealed record SceneSettings(int ObjectCount, uint Seed, ObjectDistribution Distribution,
    SearchMethod SearchMethod, bool IsPaused, bool ShowGrid, float Radius);
