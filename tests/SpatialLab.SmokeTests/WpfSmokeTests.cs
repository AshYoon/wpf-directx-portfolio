using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using SpatialLab.GlobalEnums;
using SpatialLab.Helpers;
using SpatialLab.UserControls;
using SpatialLab.ViewModels;
using SpatialLab.Views;

namespace SpatialLab.Diagnostics;
// 진단 실행 중 WPF 바인딩 오류를 모아 테스트 실패로 보고한다.
internal sealed class BindingErrorListener : TraceListener
{
    private readonly StringBuilder messages = new();
    private readonly SourceLevels oldLevel;
    public BindingErrorListener()
    {
        oldLevel = PresentationTraceSources.DataBindingSource.Switch.Level;
        PresentationTraceSources.DataBindingSource.Switch.Level = SourceLevels.Error;
        PresentationTraceSources.DataBindingSource.Listeners.Add(this);
    }
    public override void Write(string? message) => messages.Append(message);
    public override void WriteLine(string? message) => messages.AppendLine(message);
    public void ThrowIfAny()
    {
        if (messages.Length != 0) throw new InvalidOperationException("WPF binding errors: " + messages);
    }
    protected override void Dispose(bool disposing)
    {
        PresentationTraceSources.DataBindingSource.Listeners.Remove(this);
        PresentationTraceSources.DataBindingSource.Switch.Level = oldLevel;
        base.Dispose(disposing);
    }
}
// 실제 WPF 바인딩과 DLL을 연결해 입력·통계·창 수명·캡처를 검증한다.
internal static class WpfSmokeTests
{
    private static void Require(bool value,string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    public static async Task RunAsync(MainWindow window,MainViewModel viewModel,string output)
    {
        T Control<T>(string name) where T:FrameworkElement =>
            window.FindName(name) as T ?? throw new InvalidOperationException("Missing control: " + name);
        var viewport = Control<DirectXHost>("Viewport");
        var countChoice = Control<ComboBox>("CountChoice");
        var distributionChoice = Control<ComboBox>("DistributionChoice");
        var modeChoice = Control<ComboBox>("ModeChoice");
        var radiusSlider = Control<Slider>("RadiusSlider");
        var gridCheck = Control<CheckBox>("GridCheck");
        var pauseButton = Control<Button>("PauseButton");
        var resetButton = Control<Button>("ResetButton");
        async Task Settle()
        {
            window.UpdateLayout();
            await window.Dispatcher.InvokeAsync(() => {},DispatcherPriority.ApplicationIdle);
            await Task.Delay(80);
            Require(!viewModel.HasError,viewModel.StatusText);
            Require(viewport.IsReady && viewModel.IsReady,"Viewport or VM not ready.");
        }
        void Click(Button button)
        {
            Require(button.Command is not null && button.Command.CanExecute(button.CommandParameter),"Bound command unavailable.");
            button.Command!.Execute(button.CommandParameter);
        }
        await Settle();
        Require(ReferenceEquals(pauseButton.Command,viewModel.PauseCommand),"Pause command binding.");
        Require(ReferenceEquals(resetButton.Command,viewModel.ResetCommand),"Reset command binding.");
        Click(pauseButton);
        await Settle();
        Require(viewModel.IsPaused && (string)pauseButton.Content == "이동 재개","Pause label binding.");
        var counts = new uint[] {10,100,1000,5000,10000};
        for (int distribution=0;distribution<2;++distribution)
        {
            distributionChoice.SelectedIndex=distribution;
            for (int count=0;count<counts.Length;++count)
            {
                countChoice.SelectedIndex=count;
                for (int mode=0;mode<2;++mode)
                {
                    modeChoice.SelectedIndex=mode;
                    radiusSlider.Value=mode==0?12:50;
                    var stats=viewport.Tick(true);
                    Require(stats.ObjectCount==counts[count] && viewModel.SelectedObjectCount==counts[count],"Count binding to engine.");
                    Require(stats.UsesGrid==(mode==0),"Mode binding to engine.");
                    Require(viewModel.SelectedDistribution==(ObjectDistribution)distribution,"Distribution binding.");
                    Require(stats.ResultsMatch && stats.IsPaused,"Query or pause mismatch.");
                    viewport.SetQuery(0,0);
                    stats=viewport.Tick(true);
                    Require(stats.ResultsMatch && stats.QueryX==0 && stats.QueryY==100,"Viewport-to-world boundary.");
                }
            }
        }
        // Reverse direction: VM -> WPF and VM -> engine.
        viewModel.SelectedDistribution=ObjectDistribution.Uniform;
        viewModel.SelectedObjectCount=1000;
        viewModel.SelectedSearchMethod=SearchMethod.SpatialGrid;
        viewModel.Radius=12;
        await Settle();
        Require((int)countChoice.SelectedItem==1000 && modeChoice.SelectedIndex==0 &&
            distributionChoice.SelectedIndex==0 && radiusSlider.Value==12,"VM-to-view two-way binding.");
        Click(resetButton);
        var first=viewport.Tick(true);
        Click(resetButton);
        var second=viewport.Tick(true);
        Require(first.Hits==second.Hits && first.GridCandidates==second.GridCandidates,"Deterministic reset.");
        window.Width=1120;window.Height=800;
        await Settle();
        Require(viewport.Tick().ResultsMatch,"Resize.");
        window.WindowState=WindowState.Minimized;
        await Task.Delay(80);
        window.WindowState=WindowState.Normal;
        await Settle();
        Require(viewport.Tick().ResultsMatch,"Restore.");
        window.Width=1320;window.Height=880;
        await Settle();
        gridCheck.IsChecked=false;
        Require(!viewModel.ShowGrid && viewport.Tick().ResultsMatch,"Grid visibility binding.");
        gridCheck.IsChecked=true;
        var bitmap=viewport.Capture();
        var pixels=new byte[bitmap.PixelWidth*bitmap.PixelHeight*4];
        bitmap.CopyPixels(pixels,bitmap.PixelWidth*4,0);
        int mint=0,amber=0;
        for(int i=0;i<pixels.Length;i+=4)
        {
            if(pixels[i+1]>180 && pixels[i+2]<150) ++mint;
            if(pixels[i+2]>180 && pixels[i+1]>100 && pixels[i]<150) ++amber;
        }
        Require(mint>100 && amber>10,"Native geometry missing.");
        Click(pauseButton);
        viewport.Tick(true);
        await Settle();
        Require(Control<TextBlock>("HitLabel").Text==viewModel.HitText &&
            Control<TextBlock>("CandidateLabel").Text==viewModel.CandidateText &&
            Control<TextBlock>("StatusLabel").Text==viewModel.StatusText,"Statistics bindings.");
        ViewportCapture.Save(Control<FrameworkElement>("Root"),viewport,Path.Combine(output,"preview-grid.png"));
        modeChoice.SelectedIndex=1;
        viewport.Tick(true);
        await Settle();
        Require(Control<TextBlock>("ModeLabel").Text==viewModel.ModeLabel,"Mode label binding.");
        ViewportCapture.Save(Control<FrameworkElement>("Root"),viewport,Path.Combine(output,"preview-linear.png"));
        modeChoice.SelectedIndex=0;
        Require(!viewport.Tick().IsPaused && !viewModel.HasError,"Resume.");
    }
}
