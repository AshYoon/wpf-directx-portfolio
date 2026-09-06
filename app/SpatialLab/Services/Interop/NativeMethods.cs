using System.Runtime.InteropServices;

namespace SpatialLab.Services.Interop;
// SR_Settings와 같은 순서·크기를 유지하는 네이티브 입력 구조체.
[StructLayout(LayoutKind.Sequential)]
internal struct NativeSettings
{
    public uint Size, Count, Seed, Clustered, UseGrid, Paused, ShowGrid;
    public float Radius;
}
// SR_Stats에 대응하는 출력 구조체. 통계 시간의 단위는 밀리초다.
[StructLayout(LayoutKind.Sequential)]
internal struct NativeStats
{
    public uint Size, Count, Hits, LinearCandidates, GridCandidates, ResultsMatch, UseGrid, Paused;
    public double LinearMs, GridMs, RebuildMs, UpdateMs;
    public float QueryX, QueryY;
}
// C ABI 함수 이름과 cdecl 호출 규약을 고정한 P/Invoke 선언.
internal static class NativeMethods
{
    private const string Dll = "SceneRenderer.dll";
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern uint SR_GetApiVersion();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Create(out IntPtr handle);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Initialize(IntPtr handle, IntPtr window);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Resize(IntPtr handle, uint width, uint height);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Render(IntPtr handle);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Destroy(IntPtr handle);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_Configure(IntPtr handle, in NativeSettings settings, uint reset);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_SetQuery(IntPtr handle, float x, float y);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_GetStats(IntPtr handle, ref NativeStats stats);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int SR_CopyFrame(IntPtr handle, [Out] byte[] rgba, uint width, uint height);
    // 음수 HRESULT만 예외로 바꾸고 렌더 생략(S_FALSE)은 정상 상태로 처리한다.
    internal static void Check(int result)
    {
        if (result < 0) Marshal.ThrowExceptionForHR(result);
    }
}
