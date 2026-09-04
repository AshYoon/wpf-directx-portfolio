# GitHub 설정 가이드

## 현재 설정 상태

| 항목 | 값·상태 |
| --- | --- |
| 저장소 | [AshYoon/wpf-directx-portfolio](https://github.com/AshYoon/wpf-directx-portfolio) |
| 공개 범위 | Private으로 생성 |
| 로컬 경로 | `D:\WPF` |
| 브랜치 | 로컬 main, 원격은 최초 push 후 확인 |
| origin | `https://github.com/AshYoon/wpf-directx-portfolio.git` 연결 완료 |
| Git 설정 | 저장소 로컬 줄바꿈·pull·push·인증 도우미 설정 완료 |
| 최초 업로드 | Git Credential Manager 인증 진행 중 |
| CI | 앱·테스트가 생기는 M1에 구성 예정 |
| 브랜치 보호 | 미설정. 계정 요금제와 실제 CI 생성 후 검토 |
| 브랜치 자동 삭제 | 적용 안 함. 명시적 승인 부족을 사유로 자동 승인 검토에서 거부됨 |
| Issues / Squash merge | GitHub 기본값으로 활성화되어 있음을 확인 |
| 라이선스 | 미선택. 공개·배포 범위를 정한 뒤 선택 |

아래 권장 설정과 실제 적용 상태를 구분합니다. Private 저장소 생성 시 README·.gitignore·라이선스 자동 생성을 끄고, 로컬의 초기 커밋을 올리는 방식으로 구성했습니다. [GitHub 공식 생성 안내](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository).

## 적용한 로컬 Git 설정

이 저장소에만 적용하며 다른 프로젝트의 전역 설정은 변경하지 않습니다.

```powershell
git config --local core.autocrlf false
git config --local fetch.prune true
git config --local pull.ff only
git config --local push.default simple
git config --local credential.helper manager
```

`.gitattributes`가 파일별 줄바꿈을 관리합니다. 작성자 정보는 기존 Git 설정을 사용합니다. `git config user.name`, `git config user.email`로 확인하고 필요하면 로컬 설정에서 바꿉니다. 이메일을 숨기려면 GitHub Settings → Emails에 표시된 본인의 noreply 주소를 사용합니다.

## 처음부터 연결할 때의 명령

이미 초기화된 현재 폴더에서는 완료된 명령을 반복하지 않습니다. 새 환경에서 재현할 때 참고합니다.

```powershell
Set-Location D:\WPF
git init -b main
git remote add origin https://github.com/AshYoon/wpf-directx-portfolio.git
git status --short
git add README.md docs .gitignore .gitattributes .editorconfig .github
git diff --cached --stat
git diff --cached
git commit -m "docs: initialize WPF DirectX portfolio plan"
git push -u origin main
```

GitHub를 기준으로 새 PC에 가져올 때는 초기화 대신 다음 명령을 사용합니다.

```powershell
git clone https://github.com/AshYoon/wpf-directx-portfolio.git
```

## 인증

브라우저 로그인과 Git 명령의 인증은 별개입니다. HTTPS 원격 주소에 토큰을 넣지 않고 Windows Git Credential Manager의 공식 로그인으로 연결합니다. 인증 값은 채팅·저장소·설정 예제에 적지 않습니다.

GitHub CLI가 설치된 환경에서는 `gh auth login`과 `gh auth status`도 사용할 수 있습니다. 조사 당시 이 PC에는 `gh` 명령이 없어 Git과 Git Credential Manager를 사용했습니다.

## 운영 권장값

다음은 운영 권장값이며 적용 완료 목록은 아닙니다.

| 설정 | 권장값 | 이유 |
| --- | --- | --- |
| Visibility | Private | 초기 개발 |
| Default branch | main | 완성한 작업의 기준 |
| Features | Issues 사용 | 작업과 문제 기록 |
| Pull Requests | Squash merge 사용 | 작업별 이력 정리 |
| Pull Requests | Automatically delete head branches | 병합한 브랜치 정리 |
| 워크플로 권한 | `contents: read`부터 시작 | 빌드·테스트에 필요한 최소 권한 |
| Rules/Branches | CI 통과·강제 push 제한 검토 | 실제 CI를 만든 뒤 적용 |

Private 저장소의 보호 브랜치·ruleset은 요금제에 따라 제한됩니다. Free 개인 계정의 Private 저장소에서 모두 쓸 수 있다고 가정하지 않습니다. [GitHub 보호 브랜치 안내](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches).

1인 작업에서는 본인 PR에 다른 사람 승인을 필수로 요구하지 않습니다. 아직 없는 CI를 필수 검사로 지정하지 않습니다. 협업자 초대·유료 기능·Public 전환은 이번 초기 설정에 포함하지 않습니다.

## 일상적인 작업

```powershell
git switch main
git pull --ff-only
git switch -c feat/viewport-host

# 구현과 검증 후, 실제 변경한 경로를 선택하여 추가
git status --short
git add src native tests docs
git diff --cached
git commit -m "feat: host the native viewport in WPF"
git push -u origin feat/viewport-host
```

`src`, `native`, `tests`는 M1에서 생성된 뒤 사용합니다. GitHub에서 PR을 열어 동작 변화·검증·화면을 적고 main에 병합합니다. `fix/selection-sync`, `docs/build-guide`처럼 목적별 브랜치를 사용합니다.

## 초기 커밋에 포함하는 파일

- README와 가이드·계획서 3개
- .gitignore, .gitattributes, .editorconfig
- 기능·오류 이슈 템플릿과 PR 템플릿

실행 가능한 앱은 다음 단계에서 추가합니다. 원본 Git 이력·DB·접속 설정·모델 묶음은 초기 커밋에 포함하지 않습니다. `.gitignore`는 이미 커밋된 파일이나 과거 이력을 지우지 않습니다.

로컬 참고 자료는 제외된 `private-reference/`, `local-notes/`, `assets/private/`에 둘 수 있습니다. 직접 만든 샘플은 추적 가능한 `assets/samples/`에 보관합니다.

## 업로드 확인

```powershell
git status --short --branch
git remote -v
git log -1 --oneline
git rev-parse HEAD
git ls-remote origin refs/heads/main
```

작업 트리가 깨끗하고 로컬 HEAD와 원격 main 해시가 같아야 합니다. GitHub의 Private 표시와 문서도 확인합니다. 앱 빌드와 테스트는 구현 단계에서 별도로 검증합니다.
