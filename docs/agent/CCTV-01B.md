# [Claude → Codex] CCTV-01B CCTV-01A 검수 결함 수정

Claude가 CCTV-01A를 독립 검수한 결과다. [CCTV-01A](CCTV-01A.md)의 절대 금지를 그대로 적용한다. UE, UBT, UAT, 패키지 실행, Docker, mediamtx, ffmpeg 실행과 프로세스 종료는 금지다. 아래 항목 외의 수정은 하지 말고 기록만 한다.

## 수정 항목

### R1. 측정 실행기가 항상 거부됨

`Assert-DTCctvIdle`의 `^Unreal.*`가 `UnrealTraceServer.exe`도 잡는다. 이 프로세스는 UE 실행 뒤에도 상주하며, 지금도 PC에서 실행 중이다. 실행기의 첫 빌드 뒤 모든 측정이 거부된다.

- 기준: `UnrealTraceServer`는 바쁜 작업으로 보지 않는다. 다른 Editor, 빌드, 쿡, 셰이더 컴파일, 시험 프로세스는 계속 거부한다.
- README의 "TraceServer 포함" 문구도 고친다.

### R2. 게임 창을 숨겨 측정 조건이 달라짐

`Start-DTCctvOwnedProcess`가 게임에도 `-WindowStyle Hidden`을 적용한다. 숨김 창에서는 Slate가 그리지 않을 수 있어, 위젯 크기 보고, 렌더링, 스크린샷 측정이 왜곡되거나 실패할 수 있다.

- 기준: 숨김은 mediamtx와 ffmpeg에만 적용한다. 게임은 숨기지 않고 `-RenderOffScreen`으로 실행해 사용자 화면을 가리지 않는다. 대상은 `run_pending_gates.ps1`과 `fault_test.ps1`의 Windows 게임 실행이다.
- 실행기의 첫 Windows 측정에서 위젯 크기 보고(`output_w`가 0이 아님)와 비검은 스크린샷을 확인하고, 실패하면 측정을 무효로 기록한다.

### R3. 위젯을 Stop 없이 제거하면 스트림 객체가 남음

`UDTCctvView`가 GC로 파괴될 때 `UVisual::BeginDestroy` → `ReleaseSlateResources` → `Stop` 경로에서 월드가 없으면 `FindSubsystem`이 실패한다. 스트림은 닫히지만 `UDTCctvSubsystem::Streams`에 렌더 타깃과 함께 남는다. 운영 위젯을 반복해서 열고 닫으면 메모리가 계속 늘어난다.

- 기준: 월드를 찾지 못해도 스트림의 Outer인 서브시스템에서 제거한다. 서브시스템이나 스트림이 같은 GC에서 파괴 중이면 안전하게 건너뛴다.
- README에 "`Stop` 없이 위젯만 제거하면 GC 전까지 재생이 계속된다"를 적는다.

### R4. 키프레임 간격이 긴 카메라에서 잘못 재연결

첫 프레임 감시가 키프레임 도착과 무관하게 10초로 고정이고, 시간이 지나면 `bForceSoftware`까지 켠다. H.264+나 H.265+처럼 GOP가 긴 카메라는 연결마다 재연결을 반복하고, 정상인 하드웨어 디코딩도 소프트웨어로 영구 전환될 수 있다.

- 기준:
  - 첫 키프레임 패킷을 받기 전에는 디코더 고장으로 판단하지 않고 `bForceSoftware`를 켜지 않는다. 30초 안에 키프레임이 없으면 일반 재연결만 한다.
  - 키프레임 패킷을 받은 뒤 프레임이 안 나오면 기존처럼 `max(ReadTimeout, 10초)`로 판단하고, 하드웨어 사용 중이면 소프트웨어로 전환한다.
  - 판단 로직을 순수 함수로 분리해 단위 테스트를 추가한다.

### R5. Pixel Streaming 공존 게이트 범위를 사실대로 표시

현재 `ps_coexist`는 플러그인 활성화와 시작만 시험한다. 실제 NVENC 인코딩과 동시에 재생하는 것은 확인하지 못한다.

- 기준: 실행기 결과와 README, DESIGN에 "플러그인 로드 공존만 확인, 실제 스트리밍 동시 부하는 미검증"으로 표시한다. 게이트 합격 기준을 바꾸지 말고 `result.json`의 `known_limits`와 사용자 확인 항목에 넣는다.

## 검증과 산출물

- C++ 변경은 빌드 미검증이다. 커밋 메시지와 결과에 표시한다.
- PowerShell은 구문 파싱만 하고 `-Execute`는 실행하지 않는다.
- 의미 단위로 커밋한다(`CCTV-01B`). `docs/agent/CCTV-01A.md`와 이 지시서도 커밋한다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01B/RESULT.md`와 `result.json`을 쓴다.

완료 보고: 항목 / 커밋 / 항목별 변경 / 미수행·제한 / 결정 필요.
