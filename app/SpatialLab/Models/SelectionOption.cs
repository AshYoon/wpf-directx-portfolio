namespace SpatialLab.Models;
// 선택 항목의 실제 값과 화면에 표시할 이름을 연결한다.
public sealed record SelectionOption<T>(T Value, string Label);
