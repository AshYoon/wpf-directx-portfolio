using System.Diagnostics;
using System.Runtime.InteropServices;
using SpatialLab.GlobalEnums;
using SpatialLab.Interfaces;
using SpatialLab.Models;
using SpatialLab.Services.Interop;

namespace SpatialLab.Services;
// UI 모델과 네이티브 ABI 사이의 변환, 렌더러 수명과 통계 알림을 담당한다.
public sealed class NativeSceneService : INativeViewportService
{
    private readonly int ownerThread = Environment.CurrentManagedThreadId;
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private IntPtr renderer;
    private long lastPublication;
    private bool disposed;
    public bool IsReady => renderer != IntPtr.Zero;
    public event EventHandler? AvailabilityChanged;
    public event EventHandler<SceneStatistics>? StatisticsUpdated;
    public event EventHandler<string>? Faulted;

    // 생성한 UI 스레드에서만 서비스와 렌더러를 사용하도록 검사한다.
    private void VerifyAccess()
    {
        if (Environment.CurrentManagedThreadId != ownerThread)
            throw new InvalidOperationException("Scene service must use its creating UI thread.");
        ObjectDisposedException.ThrowIf(disposed, this);
    }
    // 공통 접근 검사 후 작업을 실행하고 실패를 ViewModel에 알린다.
    private T Invoke<T>(Func<T> operation)
    {
        VerifyAccess();
        try { return operation(); }
        catch (Exception error) { Faulted?.Invoke(this,error.Message); throw; }
    }
    private void RequireReady()
    {
        if (!IsReady) throw new InvalidOperationException("Native renderer is not initialized.");
    }
    // ABI 호환성을 확인해 렌더러를 연결하고 초기화 실패 시 임시 핸들을 해제한다.
    public void AttachWindow(IntPtr window) => Invoke(() =>
    {
        if (IsReady) throw new InvalidOperationException("A viewport is already attached.");
        if (NativeMethods.SR_GetApiVersion() != 2 ||
            Marshal.SizeOf<NativeSettings>() != 32 || Marshal.SizeOf<NativeStats>() != 72)
            throw new InvalidOperationException("SceneRenderer ABI 버전 또는 구조체 크기가 맞지 않습니다.");
        IntPtr created = IntPtr.Zero;
        try
        {
            NativeMethods.Check(NativeMethods.SR_Create(out created));
            NativeMethods.Check(NativeMethods.SR_Initialize(created,window));
            renderer = created;
        }
        catch
        {
            if (created != IntPtr.Zero) NativeMethods.SR_Destroy(created);
            throw;
        }
        AvailabilityChanged?.Invoke(this,EventArgs.Empty);
        return 0;
    });
    // HWND가 살아 있는 동안 렌더러를 해제하고 연결 해제를 알린다.
    public void DetachWindow()
    {
        VerifyAccess();
        if (!IsReady) return;
        NativeMethods.Check(NativeMethods.SR_Destroy(renderer));
        renderer = IntPtr.Zero;
        AvailabilityChanged?.Invoke(this,EventArgs.Empty);
    }
    // 설정의 범위를 검증하고 C 구조체로 변환해 네이티브 장면에 적용한다.
    public void Configure(SceneSettings settings, bool resetScene) => Invoke(() =>
    {
        RequireReady();
        ArgumentNullException.ThrowIfNull(settings);
        if (settings.ObjectCount < 10 || settings.ObjectCount > 10000 ||
            !Enum.IsDefined(settings.Distribution) || !Enum.IsDefined(settings.SearchMethod) ||
            !float.IsFinite(settings.Radius) || settings.Radius < 1 || settings.Radius > 50)
            throw new ArgumentOutOfRangeException(nameof(settings));
        var native = new NativeSettings
        {
            Size = (uint)Marshal.SizeOf<NativeSettings>(), Count = (uint)settings.ObjectCount, Seed = settings.Seed,
            Clustered = settings.Distribution == ObjectDistribution.Clustered ? 1u : 0u,
            UseGrid = settings.SearchMethod == SearchMethod.SpatialGrid ? 1u : 0u,
            Paused = settings.IsPaused ? 1u : 0u, ShowGrid = settings.ShowGrid ? 1u : 0u, Radius = settings.Radius
        };
        NativeMethods.Check(NativeMethods.SR_Configure(renderer,native,resetScene ? 1u : 0u));
        Publish(ReadStatistics(),true);
        return 0;
    });
    // 뷰포트의 실제 픽셀 크기를 렌더러에 전달한다.
    public void Resize(uint width, uint height) => Invoke(() =>
    {
        RequireReady();
        NativeMethods.Check(NativeMethods.SR_Resize(renderer,width,height));
        return 0;
    });
    // 한 프레임을 처리하고 네이티브 통계를 읽어 화면 갱신을 요청한다.
    public SceneStatistics Render(bool publishImmediately = false) => Invoke(() =>
    {
        RequireReady();
        NativeMethods.Check(NativeMethods.SR_Render(renderer));
        var statistics = ReadStatistics();
        Publish(statistics,publishImmediately);
        return statistics;
    });
    // 네이티브 통계를 UI가 사용하는 읽기 전용 모델로 변환한다.
    private SceneStatistics ReadStatistics()
    {
        var stats = new NativeStats {Size = (uint)Marshal.SizeOf<NativeStats>()};
        NativeMethods.Check(NativeMethods.SR_GetStats(renderer,ref stats));
        return new SceneStatistics(stats.Count,stats.Hits,stats.LinearCandidates,stats.GridCandidates,
            stats.ResultsMatch == 1,stats.UseGrid == 1,stats.Paused == 1,stats.LinearMs,stats.GridMs,
            stats.RebuildMs,stats.UpdateMs,stats.QueryX,stats.QueryY);
    }
    // 일반 통계 알림은 초당 4회로 제한하며 설정 변경 직후에는 즉시 발행한다.
    private void Publish(SceneStatistics statistics, bool force)
    {
        if (!force && clock.ElapsedMilliseconds - lastPublication < 250) return;
        lastPublication = clock.ElapsedMilliseconds;
        StatisticsUpdated?.Invoke(this,statistics);
    }
    // 마우스 입력에서 얻은 정규화 좌표를 검색 중심으로 전달한다.
    public void SetQuery(float x, float y) => Invoke(() =>
    {
        RequireReady();
        NativeMethods.Check(NativeMethods.SR_SetQuery(renderer,x,y));
        return 0;
    });
    // 진단용 RGBA 픽셀 버퍼를 할당하고 현재 네이티브 화면을 복사한다.
    public byte[] Capture(uint width, uint height) => Invoke(() =>
    {
        RequireReady();
        if (width == 0 || height == 0) throw new InvalidOperationException("Viewport is minimized.");
        var pixels = new byte[checked((int)(width * height * 4))];
        NativeMethods.Check(NativeMethods.SR_CopyFrame(renderer,pixels,width,height));
        return pixels;
    });
    public void ReportFailure(string message) { VerifyAccess(); Faulted?.Invoke(this,message); }
    // 렌더러를 해제하고 이벤트 구독을 정리한다.
    public void Dispose()
    {
        if (disposed) return;
        DetachWindow();
        disposed = true;
        AvailabilityChanged = null;
        StatisticsUpdated = null;
        Faulted = null;
    }
}
