using System.IO;
using System.Windows;
using SpatialLab.Diagnostics;
using SpatialLab.Services;
using SpatialLab.ViewModels;
using SpatialLab.Views;

namespace SpatialLab;
// 서비스와 ViewModel을 조립하고 앱 시작·종료 및 진단 실행을 관리한다.
public partial class App : Application
{
    private NativeSceneService? sceneService;
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        bool smoke = e.Args.Contains("--smoke");
        string output = e.Args.Length > 1 ? e.Args[1] : Path.Combine(AppContext.BaseDirectory,"smoke");
        using var bindingErrors = smoke ? new BindingErrorListener() : null;
        try
        {
            // ViewModel과 HWND 호스트가 같은 서비스 인스턴스를 사용하도록 연결한다.
            sceneService = new NativeSceneService();
            var viewModel = new MainViewModel(sceneService);
            var window = new MainWindow(viewModel,sceneService);
            MainWindow = window;
            if (smoke)
            {
                window.WindowStartupLocation = WindowStartupLocation.Manual;
                window.Left = -20000; window.Top = 0;
                window.ShowInTaskbar = false; window.ShowActivated = false;
            }
            window.Show();
            if (smoke)
            {
                Directory.CreateDirectory(output);
                await WpfSmokeTests.RunAsync(window,viewModel,output);
                bindingErrors!.ThrowIfAny();
                window.Close();
                File.WriteAllText(Path.Combine(output,"wpf-smoke.txt"),
                    "PASS: MVVM bindings/commands, statistics, controls, ABI, queries, reset, resize, minimize/restore, capture and shutdown.\n");
                Shutdown(0);
            }
        }
        catch (Exception error)
        {
            if (smoke)
            {
                Directory.CreateDirectory(output);
                File.WriteAllText(Path.Combine(output,"wpf-smoke.txt"),error.ToString());
            }
            else MessageBox.Show(error.Message,"Spatial Lab 실행 오류",MessageBoxButton.OK,MessageBoxImage.Error);
            Shutdown(1);
        }
    }
    // 창 종료 후 남은 서비스 자원과 이벤트 구독을 정리한다.
    protected override void OnExit(ExitEventArgs e)
    {
        sceneService?.Dispose();
        base.OnExit(e);
    }
}
