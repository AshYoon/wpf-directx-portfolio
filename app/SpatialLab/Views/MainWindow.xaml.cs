using System.Windows;
using SpatialLab.Interfaces;
using SpatialLab.ViewModels;
namespace SpatialLab.Views;
// 화면에 ViewModel과 뷰포트 서비스를 연결하는 WPF 창.
public partial class MainWindow : Window
{
    private readonly MainViewModel viewModel;
    public MainWindow(MainViewModel viewModel,INativeViewportService sceneService)
    {
        InitializeComponent();
        this.viewModel = viewModel;
        DataContext = viewModel;
        Viewport.SceneService = sceneService;
    }
    // 네이티브 뷰포트부터 해제한 뒤 ViewModel의 서비스 구독을 정리한다.
    protected override void OnClosed(EventArgs e)
    {
        Viewport.Dispose();
        viewModel.Dispose();
        base.OnClosed(e);
    }
}
