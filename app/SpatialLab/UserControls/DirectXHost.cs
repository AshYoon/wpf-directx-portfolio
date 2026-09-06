using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SpatialLab.Interfaces;
using SpatialLab.Models;
using static SpatialLab.Helpers.NativeWindowMethods;

namespace SpatialLab.UserControls;
// WPF 내부의 자식 HWND, 픽셀 크기, 마우스 입력과 프레임 요청을 관리한다.
public sealed class DirectXHost : HwndHost
{
    private IntPtr window;
    private uint pixelWidth = 1, pixelHeight = 1;
    private bool subscribed, renderFailed;
    private TimeSpan lastRenderTimestamp = TimeSpan.MinValue;
    private INativeViewportService? sceneService;
    public INativeViewportService? SceneService
    {
        get => sceneService;
        set
        {
            if (window != IntPtr.Zero) throw new InvalidOperationException("Set the service before creating the HWND.");
            sceneService = value;
        }
    }
    public bool IsReady => window != IntPtr.Zero && SceneService?.IsReady == true;
    public DirectXHost()
    {
        Loaded += (_,_) => StartRendering();
        Unloaded += (_,_) => StopRendering();
    }
    // WPF 프레임 이벤트를 구독해 UI 스레드에서 렌더링을 시작한다.
    private void StartRendering()
    {
        if (subscribed) return;
        renderFailed = false;
        lastRenderTimestamp = TimeSpan.MinValue;
        CompositionTarget.Rendering += RenderFrame;
        subscribed = true;
    }
    // 정적 Rendering 이벤트를 해제해 숨긴 뷰포트나 종료한 호스트의 호출을 막는다.
    private void StopRendering()
    {
        if (!subscribed) return;
        CompositionTarget.Rendering -= RenderFrame;
        subscribed = false;
    }
    // 자식 HWND를 만들고 렌더러를 연결하며, 연결 실패 시 생성한 자원을 정리한다.
    protected override HandleRef BuildWindowCore(HandleRef parent)
    {
        if (SceneService is null) throw new InvalidOperationException("A scene service is required.");
        window = CreateWindowEx(0,"STATIC","",0x40000000|0x10000000|0x02000000|0x04000000,
            0,0,1,1,parent.Handle,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
        if (window == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            SceneService.AttachWindow(window);
            ResizeFromClient();
            return new HandleRef(this,window);
        }
        catch
        {
            SceneService.DetachWindow();
            DestroyWindow(window);
            window = IntPtr.Zero;
            throw;
        }
    }
    // 프레임 요청 중지 → 렌더러 해제 → HWND 파괴 순서로 종료한다.
    protected override void DestroyWindowCore(HandleRef handle)
    {
        Dispatcher.VerifyAccess();
        StopRendering();
        SceneService?.DetachWindow();
        DestroyWindow(handle.Handle);
        window = IntPtr.Zero;
    }
    protected override void OnWindowPositionChanged(Rect bounds)
    {
        base.OnWindowPositionChanged(bounds);
        try { ResizeFromClient(); } catch (Exception error) { Fail(error); }
    }
    // WPF 논리 크기 대신 Win32 클라이언트의 실제 픽셀 크기를 읽는다.
    private void ResizeFromClient()
    {
        if (!IsReady) return;
        if (!GetClientRect(window,out var rectangle)) throw new Win32Exception(Marshal.GetLastWin32Error());
        uint width = (uint)Math.Max(0,rectangle.Right-rectangle.Left);
        uint height = (uint)Math.Max(0,rectangle.Bottom-rectangle.Top);
        if (width == pixelWidth && height == pixelHeight) return;
        SceneService!.Resize(width,height);
        pixelWidth = width; pixelHeight = height;
    }
    // 같은 프레임의 중복 호출과 최소화·숨김·오류 상태의 렌더링을 건너뛴다.
    private void RenderFrame(object? sender, EventArgs e)
    {
        if (e is RenderingEventArgs frame)
        {
            if (frame.RenderingTime == lastRenderTimestamp) return;
            lastRenderTimestamp = frame.RenderingTime;
        }
        if (renderFailed || !IsReady || !IsVisible || Window.GetWindow(this)?.WindowState == WindowState.Minimized) return;
        try { Tick(); } catch (Exception error) { Fail(error); }
    }
    // 프레임 처리를 멈추고 오류 내용을 서비스에 알린다.
    private void Fail(Exception error)
    {
        renderFailed = true;
        SceneService?.ReportFailure(error.Message);
    }
    // 창 크기를 맞춘 뒤 프레임을 실행한다. 진단 코드도 이 경로를 사용한다.
    internal SceneStatistics Tick(bool publishImmediately = false)
    {
        Dispatcher.VerifyAccess();
        if (!IsReady) throw new InvalidOperationException("Viewport is not initialized.");
        ResizeFromClient();
        return SceneService!.Render(publishImmediately);
    }
    // UI 스레드 접근을 확인하고 검색 중심 변경을 서비스에 전달한다.
    internal void SetQuery(float x,float y)
    {
        Dispatcher.VerifyAccess();
        if (IsReady) SceneService!.SetQuery(x,y);
    }
    // 네이티브 RGBA를 WPF BGRA 이미지로 변환해 진단 캡처에 사용한다.
    internal BitmapSource Capture()
    {
        Dispatcher.VerifyAccess();
        ResizeFromClient();
        var pixels = SceneService!.Capture(pixelWidth,pixelHeight);
        for (int i=0;i<pixels.Length;i+=4) (pixels[i],pixels[i+2]) = (pixels[i+2],pixels[i]);
        var bitmap = BitmapSource.Create((int)pixelWidth,(int)pixelHeight,96,96,
            PixelFormats.Bgra32,null,pixels,(int)pixelWidth*4);
        bitmap.Freeze();
        return bitmap;
    }
    // Win32 클릭·드래그 메시지를 정규화 좌표로 변환하고 마우스 캡처를 관리한다.
    protected override IntPtr WndProc(IntPtr hwnd,int message,IntPtr wParam,IntPtr lParam,ref bool handled)
    {
        const int LeftDown=0x0201, LeftUp=0x0202, MouseMove=0x0200, Erase=0x0014;
        if (message == Erase) { handled=true; return new IntPtr(1); }
        try
        {
            if (IsReady && (message == LeftDown || (message == MouseMove && ((long)wParam&1)!=0)))
            {
                int x=(short)((long)lParam&0xffff), y=(short)(((long)lParam>>16)&0xffff);
                SetQuery(x/(float)Math.Max(1u,pixelWidth),y/(float)Math.Max(1u,pixelHeight));
                if (message == LeftDown) SetCapture(hwnd);
                handled=true;
                return IntPtr.Zero;
            }
            if (message == LeftUp) { ReleaseCapture();handled=true;return IntPtr.Zero; }
        }
        catch (Exception error) { ReleaseCapture();Fail(error);handled=true;return IntPtr.Zero; }
        return base.WndProc(hwnd,message,wParam,lParam,ref handled);
    }
}
