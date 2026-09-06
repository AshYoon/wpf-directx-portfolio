using SpatialLab.Models;
namespace SpatialLab.Interfaces;
// HwndHost용 계약. 호스트는 HWND를 소유하고 서비스는 렌더러를 소유한다.
public interface INativeViewportService : ISceneService, IDisposable
{
    void AttachWindow(IntPtr window);
    void DetachWindow();
    void ReportFailure(string message);
    void Resize(uint width, uint height);
    SceneStatistics Render(bool publishImmediately = false);
    void SetQuery(float x, float y);
    byte[] Capture(uint width, uint height);
}
