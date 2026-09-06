using SpatialLab.Models;
namespace SpatialLab.Interfaces;
// ViewModel이 사용하는 설정·통계 계약. HWND와 네이티브 포인터를 노출하지 않는다.
public interface ISceneService
{
    bool IsReady { get; }
    event EventHandler? AvailabilityChanged;
    event EventHandler<SceneStatistics>? StatisticsUpdated;
    event EventHandler<string>? Faulted;
    void Configure(SceneSettings settings, bool resetScene);
}
