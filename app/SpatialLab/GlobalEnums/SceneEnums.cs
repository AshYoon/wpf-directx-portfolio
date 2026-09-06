namespace SpatialLab.GlobalEnums;
// 객체를 월드 전체에 분산하거나 중앙에 밀집시키는 초기 배치 방식.
public enum ObjectDistribution { Uniform, Clustered }
// 후보를 시각화할 검색 방식. 정확성 비교를 위해 두 검색은 모두 실행한다.
public enum SearchMethod { SpatialGrid, Linear }
