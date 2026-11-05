# UE-DT-CCTV-Plugin 에이전트 규칙

DT 프로젝트에서 RTSP CCTV를 재생하는 재사용 플러그인(`DTCctv`)입니다. 설계는 [docs/DESIGN.md](docs/DESIGN.md)를 따릅니다.

## 역할 (DT 협업 운영표)

- Claude: 계획, 지시서 작성, 독립 검수, 승인 범위의 머지
- Codex: 구현, 빌드, 테스트, 측정, 커밋, 결과 작성
- 사용자: 범위와 실패 수용 결정, 현장·서버 검증

## 규칙

- 지시서의 한 항목만 작업합니다. 범위 밖 문제는 고치지 말고 보고합니다.
- 지시서의 예비안은 재질문 없이 적용하고 선택과 근거를 기록합니다. 계약 변경 등 진짜 차단일 때만 멈춥니다.
- 테스트 기대값과 합격 기준을 완화하지 않습니다. 실패와 미수행을 PASS로 표시하지 않습니다.
- 명시한 파일만 stage합니다(`git add -A` 금지). push, PR, merge는 지시서가 허용할 때만 합니다.
- 비밀번호, 토큰, 카메라 계정을 출력하거나 커밋하지 않습니다. 로그의 URL은 `DTCctvMaskUrlCredentials`로 가립니다.
- 빌드, Editor, 측정은 한 번에 하나만 실행합니다. 측정 중에는 다른 GPU·빌드 작업을 하지 않습니다.
- 진행 보고는 20분마다, 변화가 없으면 한 줄로 합니다. 결정 필요, 이미지 산출, 완료, 실패는 즉시 보고합니다.
- 완료하면 `Saved/Reports/<ID>/result.json`과 `RESULT.md`를 작성합니다. `Saved/`는 커밋하지 않습니다.

## 플러그인 경계

- 운영 프로젝트와 DTCore에 의존하지 않습니다. 프로젝트 맵, 서버 주소, 카메라 주소를 코드에 넣지 않습니다.
- 블루프린트와 에셋을 플러그인에 넣지 않습니다. 운영 프로젝트가 `UDTCctvView`와 `UDTCctvStream`으로 위젯을 만듭니다.
- FFmpeg 바이너리는 LGPL 빌드만 사용하며 동적 링크합니다. 교체하면 `Source/ThirdParty/DTCctvFFmpeg/README.md`를 갱신합니다.
- 게임 스레드를 네트워크 작업으로 막지 않습니다. `FFmpeg` 헤더는 Private에만 둡니다.
- 주기적 재연결이나 시간 제한 종료를 넣지 않습니다. 재연결은 실제 장애 때만 합니다.

## 코드 스타일

- UE 스타일, 탭 들여쓰기, Allman 브레이스. 주석은 한국어로 필요한 곳에만 씁니다.
- 접두사는 `DTCctv`를 사용합니다.

## 빌드와 검증

```powershell
TestHost\Scripts\setup_testhost.ps1
& "C:\Program Files\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" DTCctvTestHostEditor Win64 Development "-Project=C:\Unreal Projects\UE-DT-CCTV-Plugin\TestHost\DTCctvTestHost.uproject" -WaitMutex -NoHotReload
```

- 단위 테스트: `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests DTCctv; Quit" -unattended -nullrhi`
- 다채널 측정: `-game -CctvUrl=... -CctvCount=16 -CctvSeconds=... -CctvCsv=... -CctvShot=...` ([DTCctvHarnessSubsystem.h](TestHost/Source/DTCctvTestHost/DTCctvHarnessSubsystem.h))
- 장애 복구: [TestHost/Scripts/fault_test.ps1](TestHost/Scripts/fault_test.ps1)
