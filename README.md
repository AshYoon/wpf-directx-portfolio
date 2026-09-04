# WPF DirectX Portfolio

WPF와 C++ Direct3D 11을 연결하는 **3D 시나리오 편집기**를 만드는 포트폴리오 프로젝트입니다. ATTS-TAC 개발 경험을 바탕으로 객체 배치, 속성 편집, 저장·불러오기 흐름을 독립 실행 가능한 작은 제품으로 재작성합니다.

> 현재 단계: 기획 및 저장소 초기 구성. 실행 가능한 WPF 앱과 렌더러는 아직 구현되지 않았습니다. GitHub 저장소는 처음부터 **Private**으로 운영합니다.

## 문서

| 문서 | 내용 |
| --- | --- |
| [재작성 가이드](docs/01-portfolio-guide.md) | 기존 구조 분석, MVP 범위, 목표 아키텍처, 구현 원칙 |
| [개발 계획서](docs/02-development-plan.md) | 단계별 작업, 완료 기준, 우선순위, 데모·검증 계획 |
| [GitHub 설정 가이드](docs/03-github-setup.md) | 비공개 저장소, 최초 업로드, 브랜치 운영, 설정 상태 |

## 만들 제품

1. 샘플 장면을 열고 카메라를 이동합니다.
2. 도형을 배치하고, 화면 또는 목록에서 선택합니다.
3. 위치·회전·크기를 속성 패널에서 편집합니다.
4. JSON으로 저장한 뒤 다시 열어 같은 장면을 복원합니다.
5. 되돌리기·다시 실행과 오류 안내를 제공합니다.

초기 샘플은 직접 생성한 도형과 가상의 데이터로 구성합니다. DB 서버 없이 실행하고, 네트워크 연동은 확장 단계에서 다룹니다.

## 기술 방향 — 구현 예정

| 영역 | 선택 |
| --- | --- |
| 데스크톱 UI | C#, WPF, .NET 10 LTS, x64 |
| 상태·명령 | MVVM, 생성자 주입, CommunityToolkit.Mvvm |
| 렌더링 | C++17, Direct3D 11, Win32 HWND |
| 상호 운용 | HwndHost, 명시적인 C ABI, P/Invoke |
| 저장 | System.Text.Json, 버전이 있는 시나리오 형식 |
| 검증 | 도메인·저장소 자동 테스트, 네이티브 빌드, Windows 데모 검증 |

새 프로젝트는 .NET 10 LTS를 목표로 합니다. 조사 시 로컬에는 .NET SDK 9.0.311이 설치되어 있었으며, 실제 앱 구현 전에 .NET 10 SDK와 필요한 C++ 빌드 도구를 준비해야 합니다. 지원 기간의 근거는 [Microsoft .NET 지원 정책](https://dotnet.microsoft.com/en-us/platform/support/policy)입니다.

## 구현할 구조

아래의 `src`, `native`, `tests`는 계획상 경로이며 아직 생성하지 않았습니다.

```text
src/
  SceneEditor.App/                 # WPF UI, ViewModel, 조립 지점
  SceneEditor.Core/                # 장면 모델, 편집 규칙, 인터페이스
  SceneEditor.Infrastructure/      # JSON 저장, 설정, 로그
  SceneEditor.Rendering/           # HwndHost, P/Invoke, 렌더러 어댑터
native/
  SceneRenderer/                  # Direct3D 11 DLL, C ABI
tests/
  SceneEditor.Core.Tests/
  SceneEditor.Infrastructure.Tests/
assets/samples/                    # 직접 만든 도형·샘플 장면
docs/
.github/
```

## 진행 상태

- [x] 재작성 가이드 및 단계별 계획서
- [x] Git 제외 규칙, 줄바꿈 규칙, PR·이슈 템플릿
- [ ] GitHub Private 저장소 생성 및 최초 push 확인
- [ ] M1: WPF·네이티브 빌드 기반과 기본 장면
- [ ] M2: 객체 배치·선택·속성 편집
- [ ] M3: 저장·복원·되돌리기
- [ ] M4: 안정성, 측정 결과, 데모와 배포

완료한 기능만 체크하고, 목표 성능은 실측 결과와 구분해서 기록합니다.

## 작업 시작

현재는 문서 단계이므로 실행 명령은 없습니다. 첫 구현 작업은 [계획서의 M1](docs/02-development-plan.md#m1--빌드-기반과-첫-장면)을 따릅니다. 앱이 생기면 이 항목에 검증된 설치·빌드·실행 명령, 요구 사양, 데모 파일 위치를 추가합니다.

## 소스와 에셋 관리

기존 프로젝트는 로컬 참고 자료로 사용합니다. 새 저장소에는 재작성한 코드와 사용 범위가 확인된 자료를 넣고, 기존 DB·접속 설정·사내 문서·대용량 모델 묶음은 가져오지 않습니다. 에셋을 추가할 때 출처와 사용 조건을 기록합니다. 라이선스는 공개 전 배포 범위를 정할 때 선택합니다.
