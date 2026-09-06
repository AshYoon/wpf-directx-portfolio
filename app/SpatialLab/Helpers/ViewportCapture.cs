using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SpatialLab.UserControls;
namespace SpatialLab.Helpers;
// WPF 영역과 네이티브 뷰포트를 합성해 앱 진단 이미지를 저장한다.
internal static class ViewportCapture
{
    // HwndHost는 WPF 캡처에 포함되지 않으므로 back buffer 이미지를 해당 위치에 합성한다.
    public static void Save(FrameworkElement root,DirectXHost viewport,string path)
    {
        root.UpdateLayout();
        int width=(int)Math.Ceiling(root.ActualWidth), height=(int)Math.Ceiling(root.ActualHeight);
        var offset=VisualTreeHelper.GetOffset(root);
        int x=(int)Math.Round(offset.X), y=(int)Math.Round(offset.Y);
        var surface=new RenderTargetBitmap(width+x,height+y,96,96,PixelFormats.Pbgra32);
        surface.Render(root);
        var background=new CroppedBitmap(surface,new Int32Rect(x,y,width,height));
        var native=viewport.Capture();
        var area=viewport.TransformToAncestor(root).TransformBounds(new Rect(viewport.RenderSize));
        var drawing=new DrawingVisual();
        using(var context=drawing.RenderOpen())
        {
            context.DrawImage(background,new Rect(0,0,width,height));
            context.DrawImage(native,area);
        }
        var composite=new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32);
        composite.Render(drawing);
        var encoder=new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(composite));
        using var stream=File.Create(path);
        encoder.Save(stream);
    }
}
