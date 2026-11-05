# [Claude → Codex] CCTV-01C 빌드 재검증과 보류 측정 실행

사용자가 UE 사용 보류를 해제하고 작업 재개를 요청했다. CCTV-01A·01B 코드를 실제로 빌드하고 [CCTV-01](CCTV-01.md) 항목 4~8을 측정한다.

## 절대 금지

- push, PR, merge, 원격 변경
- 이름으로 프로세스 종료. 이 작업이 시작한 프로세스의 PID와 이 작업이 만든 컨테이너 ID만 정리한다.
- 기존 Docker 이미지와 컨테이너 변경, 이미지 다운로드
- 토큰, 비밀번호, 인증 URL의 출력과 커밋
- 합격 기준 완화. 기준을 못 넘으면 FAIL로 기록한다.
- 측정 병렬 실행. 빌드와 측정, 측정과 측정은 겹치지 않는다.

## 다른 작업과의 공존

다른 세션도 이 PC의 Unreal을 쓴다. 시작 전과 각 측정 전에 `Assert-DTCctvIdle`로 확인한다.

- 다른 Editor, 빌드, 쿡, 셰이더 컴파일이 보이면 측정을 시작하지 않는다. 종료시키지 않는다.
- 5분 간격으로 최대 30분 기다리고, 그래도 바쁘면 완료된 결과를 저장하고 `BLOCKED_BY_OTHER_WORK`로 끝낸다.
- 측정 중 다른 작업이 겹쳐 수치가 오염되면 그 측정을 무효로 하고 다시 실행한다.

## 수행 순서

### 1. 실행기 사전 수정

- `run_pending_gates.ps1`의 Linux Pixel Streaming 결과(`6_ps_linux`)는 `pixel_streaming_mounted`가 참일 때만 PASS다. 다른 Linux 케이스의 기준은 바꾸지 않는다.
- 실행기 함수가 `$summary` 외의 값을 파이프라인으로 내보내 결과가 배열이 되지 않는지 확인하고, 그렇다면 고친다.
- 위 수정 후 구문 파싱을 한 뒤 커밋한다.

### 2. 빌드와 단위 테스트

Editor 빌드, 단위 테스트 전체(신규 FrameMetadata, OutputWatchdog 포함), Win64·Linux 패키징을 다시 수행한다. 실패하면 원인을 고치고 다시 빌드한다. 이전 패키지를 최신 결과로 쓰지 않는다.

### 3. 측정

CCTV-01 항목 4~8의 게이트를 이 순서로 실행한다. 60분 soak는 마지막이다.

| 항목 | 게이트 | 합격 기준 |
|---|---|---|
| 4 | package_win_16ch | decode ≥ 58, display ≥ 58, dropped 0, reconnect 0, 60초 이상, 첫 측정에서 output_w > 0과 비검은 스크린샷 |
| 5 | package_linux_run | RHEL 8 컨테이너에서 FFmpeg 로드, 프레임 디코딩, 전 채널 Playing |
| 6 | ps_coexist | Windows·Linux 모두 Pixel Streaming 마운트 상태에서 전 채널 Playing. 범위는 로드 공존만 |
| 7 | resize_substream | drag, extremes, subset, disconnected, substream. PNG를 직접 열어 찌그러짐, 검은 화면, 깨짐이 없는지 확인 |
| 8 | fault, soak | 서버 중단 후 자동 복구, close-open 5회, 60분 dropped 0, reconnect 0, 스레드 일정, 메모리 추세 |

`REVIEW_REQUIRED` 항목은 Codex가 자료를 직접 확인해 판정과 근거를 적는다. Claude가 다시 검수한다.

### 4. 실패 시 대응

- 코드 결함이면 고치고 새 커밋을 만든 뒤, 영향을 받는 빌드와 게이트를 다시 실행한다. 같은 게이트의 수정 시도는 3회까지다.
- C++를 고쳤으면 패키지를 다시 만들고, 이미 통과한 게이트 중 영향을 받는 것도 다시 실행한다. soak는 코드가 확정된 뒤의 결과만 인정한다.
- 대체 계획:
  - Linux GPU(Vulkan) 실행 불가: 소프트웨어(`-nullrhi -CctvSoftware`)로 로더와 디코딩을 확인하고, 렌더링 미검증으로 표시한다. add-host 변형도 시도한다.
  - `-RenderOffScreen`에서 측정이 무효: 일반 창으로 실행해 재측정하고, 창을 띄운 사실과 이유를 기록한다.
  - 16채널 60fps 미달: 병목(디코드, 업로드, 표시)을 CSV로 구분하고 수정을 시도한다. 3회 후에도 미달이면 FAIL로 수치와 병목을 기록하고 나머지 게이트를 계속한다.
- 한 게이트의 실패로 나머지 게이트를 건너뛰지 않는다. 단, 빌드 실패는 예외다.

## 산출물

- 의미 단위 커밋(`CCTV-01C`). 이 지시서도 커밋한다.
- `Saved/Reports/CCTV-01C/RESULT.md`와 `result.json`: 게이트별 판정, 수치, 증거 경로, 커밋, 실패와 재시도 이력, `known_limits`, 사용자 확인 항목.
- 게이트가 끝날 때마다 `Saved/Reports/CCTV-01C/progress.md`에 한 줄을 추가한다(시각, 게이트, 판정, 핵심 수치). Claude가 이 파일로 중간 보고한다.
- 결과에 없는 수치를 만들지 않는다. 실행하지 않은 항목은 NOT_RUN으로 적는다.

완료 보고: 항목 / 커밋 / 게이트별 판정과 수치 / 미수행·제한 / 결정 필요.
