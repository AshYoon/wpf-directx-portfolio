using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SpatialLab.GlobalEnums;
using SpatialLab.Interfaces;
using SpatialLab.Models;

namespace SpatialLab.ViewModels;
// 화면 설정, 명령과 통계 표시를 관리하며 WPF 컨트롤이나 DLL을 직접 호출하지 않는다.
public partial class MainViewModel : ObservableObject, IDisposable
{
    private readonly ISceneService sceneService;
    private bool disposed;

    public IReadOnlyList<int> ObjectCounts { get; } = new[] {10,100,1000,5000,10000};
    public IReadOnlyList<SelectionOption<ObjectDistribution>> Distributions { get; } = new[]
    {
        new SelectionOption<ObjectDistribution>(ObjectDistribution.Uniform,"균등 분포"),
        new SelectionOption<ObjectDistribution>(ObjectDistribution.Clustered,"중앙 밀집")
    };
    public IReadOnlyList<SelectionOption<SearchMethod>> SearchMethods { get; } = new[]
    {
        new SelectionOption<SearchMethod>(SearchMethod.SpatialGrid,"공간 그리드"),
        new SelectionOption<SearchMethod>(SearchMethod.Linear,"전체 순회")
    };

    // Toolkit이 변경 알림을 생성하고, 아래 변경 훅에서 최신 설정을 서비스에 전달한다.
    [ObservableProperty] private int selectedObjectCount = 1000;
    [ObservableProperty] private ObjectDistribution selectedDistribution;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ModeLabel))]
    private SearchMethod selectedSearchMethod;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(RadiusLabel))]
    private double radius = 12;
    [ObservableProperty] private bool showGrid = true;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PauseLabel))]
    private bool isPaused;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanInteract),nameof(StatusText))]
    [NotifyCanExecuteChangedFor(nameof(PauseCommand),nameof(ResetCommand))]
    private bool isReady;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError),nameof(CanInteract),nameof(StatusText))]
    [NotifyCanExecuteChangedFor(nameof(PauseCommand),nameof(ResetCommand))]
    private string? errorMessage;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HitText),nameof(MatchText),nameof(HasMismatch),
        nameof(CandidateText),nameof(LinearTimeText),nameof(GridTimeText),
        nameof(BuildTimeText),nameof(TotalTimeText),nameof(StatusText))]
    private SceneStatistics? statistics;

    public bool HasError => ErrorMessage is not null;
    public bool CanInteract => IsReady && !HasError && !disposed;
    public string ModeLabel => SelectedSearchMethod == SearchMethod.SpatialGrid ? "공간 그리드" : "전체 순회";
    public string RadiusLabel => Radius.ToString("F1");
    public string PauseLabel => IsPaused ? "이동 재개" : "일시정지";
    public string HitText => Statistics is { } s ? $"{s.Hits:N0}개" : "—";
    public bool HasMismatch => Statistics is {ResultsMatch:false};
    public string MatchText => Statistics is null ? "초기화 중" :
        HasMismatch ? "검색 결과 불일치" : "두 방식의 검색 결과가 일치합니다.";
    public string CandidateText => Statistics is { } s ? $"{s.LinearCandidates:N0} → {s.GridCandidates:N0}" : "—";
    public string LinearTimeText => Time(Statistics?.LinearMs);
    public string GridTimeText => Time(Statistics?.GridMs);
    public string BuildTimeText => Time(Statistics?.RebuildMs);
    public string TotalTimeText => Time(Statistics is { } s ? s.GridMs + s.RebuildMs : null);
    public string StatusText => HasError ? "렌더링 중단: " + ErrorMessage : !IsReady ? "네이티브 뷰포트 연결 중" :
        Statistics is { } s ? $"{(s.IsPaused ? "일시정지" : "실행 중")}  ·  객체 {s.ObjectCount:N0}개  ·  검색 중심 ({s.QueryX:F1}, {s.QueryY:F1})" : "장면 준비 중";
    private static string Time(double? milliseconds) => milliseconds is { } value ? $"{value*1000:F2} µs" : "—";

    // 서비스의 준비 상태·통계·오류 이벤트를 화면 속성으로 연결한다.
    public MainViewModel(ISceneService sceneService)
    {
        this.sceneService = sceneService;
        sceneService.AvailabilityChanged += OnAvailabilityChanged;
        sceneService.StatisticsUpdated += OnStatisticsUpdated;
        sceneService.Faulted += OnFaulted;
        OnAvailabilityChanged(sceneService, EventArgs.Empty);
    }
    // 지원하지 않는 설정은 속성이 바뀌기 전에 거절한다.
    partial void OnSelectedObjectCountChanging(int value)
    {
        if (!ObjectCounts.Contains(value)) throw new ArgumentOutOfRangeException(nameof(value));
    }
    partial void OnRadiusChanging(double value)
    {
        if (!double.IsFinite(value) || value < 1 || value > 50) throw new ArgumentOutOfRangeException(nameof(value));
    }
    partial void OnSelectedDistributionChanging(ObjectDistribution value)
    {
        if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value));
    }
    partial void OnSelectedSearchMethodChanging(SearchMethod value)
    {
        if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value));
    }
    // 설정 변경은 ApplySettings를 통해 같은 서비스 호출 경로로 모은다.
    partial void OnSelectedObjectCountChanged(int value) => ApplySettings(false);
    partial void OnSelectedDistributionChanged(ObjectDistribution value) => ApplySettings(false);
    partial void OnSelectedSearchMethodChanged(SearchMethod value) => ApplySettings(false);
    partial void OnRadiusChanged(double value) => ApplySettings(false);
    partial void OnShowGridChanged(bool value) => ApplySettings(false);
    partial void OnIsPausedChanged(bool value) => ApplySettings(false);

    // 객체 이동을 일시정지하거나 다시 시작한다.
    [RelayCommand(CanExecute = nameof(CanInteract))]
    private void Pause() { if (CanInteract) IsPaused = !IsPaused; }
    // 현재 설정을 유지하면서 초기 배치와 검색 중심을 복원한다.
    [RelayCommand(CanExecute = nameof(CanInteract))]
    private void Reset() { if (CanInteract) ApplySettings(true); }

    // 현재 UI 설정을 모델로 묶어 전달하고 실패하면 오류 상태를 표시한다.
    private void ApplySettings(bool reset)
    {
        if (!CanInteract) return;
        try
        {
            sceneService.Configure(new SceneSettings(SelectedObjectCount,42,SelectedDistribution,
                SelectedSearchMethod,IsPaused,ShowGrid,(float)Radius),reset);
        }
        catch (Exception error) { ErrorMessage = error.Message; }
    }
    // 뷰포트 연결 시 보관한 설정을 적용하고 연결 해제 시 통계를 비운다.
    private void OnAvailabilityChanged(object? sender, EventArgs e)
    {
        IsReady = sceneService.IsReady;
        if (IsReady) { ErrorMessage = null; ApplySettings(true); }
        else Statistics = null;
    }
    private void OnStatisticsUpdated(object? sender, SceneStatistics value) => Statistics = value;
    private void OnFaulted(object? sender, string message) => ErrorMessage = message;

    // 서비스 이벤트 구독을 해제하고 종료 후 명령 실행을 막는다.
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        sceneService.AvailabilityChanged -= OnAvailabilityChanged;
        sceneService.StatisticsUpdated -= OnStatisticsUpdated;
        sceneService.Faulted -= OnFaulted;
        OnPropertyChanged(nameof(CanInteract));
        PauseCommand.NotifyCanExecuteChanged();
        ResetCommand.NotifyCanExecuteChanged();
    }
}
