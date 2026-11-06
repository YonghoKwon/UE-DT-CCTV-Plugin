# CCTV-01W 창 상태 진단

Windows 11 Pro 10.0.26200, UE5.3, C++ `f6192e9`, 동일 Win64 패키지에서16채널1080p60·게임60fps·1920×1080으로 측정했다. 준비/워밍업10초 뒤 연속90초 이상을 분석했다. 조건은 직렬 실행했고 측정 구간에서 CUA를 호출하지 않았다. 창 준비는 Win32 API로 t8 이전에 끝냈고 관측 중에는 창 상태를 읽기만 했다. 시스템 전원 설정·레지스트리·플러그인 기본값은 바꾸지 않았다.

|조건|디코드 fps|표시 fps|최악 채널 p95 ms|교체율|게임 fps|CPU %|GPU %|
|---|---:|---:|---:|---:|---:|---:|---:|
|W1 포그라운드|59.9764|59.9037|36|0.1236%|59.9615|29.56|47.53|
|W2 일부 가림·비포커스|59.9341|50.0652|46|16.4845%|50.1231|27.98|42.59|
|W3 최소화|59.9889|50.6028|43|15.6279%|50.6489|30.25|43.29|
|W4 오프스크린|59.9764|59.8483|36|0.2149%|59.9604|30.51|46.06|
|W5 W2 + 속도/타이머 제한 해제|60.0104|59.9293|37|0.1328%|59.9604|29.91|47.41|
|W5 속도 제한만 해제|60.0333|48.5072|46|19.1995%|48.5011|30.55|43.06|
|W5 타이머 요청만 존중|59.9778|59.9536|34|0.0404%|59.9879|27.79|47.29|

표시 FPS는 렌더 명령 등록 횟수이고 모니터 주사/최종 표시 횟수는 아니다. frame age는 디코더 반환~등록 구간이다. CPU는 전체 논리CPU 수로 정규화한 해당 프로세스 평균이다. GPU는 송출 NVENC도 포함한 장치 전체5초 표본이다. 창·포커스·전원은 약1초 표본이며 표본 사이의 모든 이벤트까지 증명하지 않는다. 모든 유효 실행에서 손실·재연결·디코딩 오류0, 전16채널 Playing이었다.

## 원인과 한계

이 환경에서 관측된 근접 원인은 **백그라운드 프로세스의 타이머 해상도 요청 처리 정책**이다. 동일 가림·비포커스에서 실행 속도 제한만 명시적으로 끄면48.5072fps였고, 타이머 요청을 존중하도록 명시하면59.9536fps였다. 두 비트를 함께 제어한 경우도59.9293fps였다. 따라서 단순 CPU 실행 속도 제한이나 DWM 합성 부하만으로 이 차이를 설명하기 어렵다. CUA 호출 없이 W2에서 저하가 재현돼 UI 자동화 도구는 재현의 필수 조건이 아니다.

Microsoft는 Windows 11에서 비가시/최소화·비가청 프로세스의 타이머 해상도 요청이 자동으로 무시될 수 있고, `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`을 명시적으로 끄면 기존 요청을 존중한다고 설명한다. [SetProcessInformation 공식 문서](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation)

원래 `ControlMask=0, StateMask=0`은 OS 자동 관리다. 이것을 “제한이 확실히 꺼져 있다”로 해석하지 않는다. 실험은 `ProcessPowerThrottling=4`, 구조체 Version1, 실행 속도 비트1·타이머 무시 비트4를 사용했다. W5는5/0, 속도만1/0, 타이머만4/0으로 설정했고 관측 구간 뒤 t103 이후 원래0/0으로 복원했다. API 반환과 재조회로 복원을 확인했다. [구조체](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_power_throttling_state), [정보 클래스](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ne-processthreadsapi-process_information_class)

타이머 단독 첫 실행은 끝부분에 포그라운드가 게임으로 바뀌어 무효다. 원인을 단정하지 않았고 전체 원자료를 보존한 뒤 동일 조건을 한 번 재측정했다. 표에는 재측정만 썼다. 01J 당시의 포커스 기록은 없어 그때의 정확한 창 상태를 소급 확정하지 않는다. OS 내부 자동 휴리스틱 자체를 직접 관찰한 것도 아니다. 결과 범위는 이 PC·버전·부하다.

## 재현 도구

```powershell
# 먼저 Saved/Reports/CCTV-01W/checkpoint.json의 기존 도구 경로를 준비한다.
TestHost/Scripts/window_state_test.ps1 -Condition W1 -Tag foreground-new
TestHost/Scripts/window_state_test.ps1 -Condition W2 -Tag covered-new
TestHost/Scripts/window_state_test.ps1 -Condition W3 -Tag minimized-new
TestHost/Scripts/window_state_test.ps1 -Condition W4 -Tag offscreen-new
TestHost/Scripts/window_state_test.ps1 -Condition W5 -Tag power-new
TestHost/Scripts/window_state_test.ps1 -Condition W5 -Tag speed-new -PowerBits 1
TestHost/Scripts/window_state_test.ps1 -Condition W5 -Tag timer-new -PowerBits 4
```

각 호출은 전체105초 실행 중 초기10초를 제외한90초 이상의 구간을 분석하기 위한 원자료를 수집한다. `execution.json`, `samples*.csv`, `window-state.jsonl`, `gpu.csv`, `window-monitor-result.json`을 저장한다. W3는 최소화 유지에 영향을 주지 않도록 PNG를 요청하지 않는다. 소유 PID·생성 시각·경로를 확인한 게임 프로세스만 바꾸고 자체 중립 가림 창을 사용한다. 기존 자료는 덮어쓰지 않는다. 다른 UE/빌드가 있으면5분 간격 최대30분 기다리고, 실행 중 겹치면 무효로 한다. 수집 스크립트는 결과 JSON의 측정 완료를 성능 PASS로 해석하지 않는다.

## 운영 권장과 사용자 결정

- 화면이 필요한 전용 운영 창은 실제 포그라운드를 유지한다. TopMost만으로 포커스가 보장되지는 않는다. 포커스를 잃을 수 있는 대시보드에서는 표시 FPS를 감시한다.
- 화면 없는 Pixel Streaming 서버의 실행 후보는 검증한 오프스크린 경로다. 현재 자료는 CCTV 렌더링/플러그인 로드 범위이며 실제 동시 인코딩 부하와 별도 시스템의 안정성은 추가 확인한다. 기존 오프스크린 종료 오류 이력도 유지한다.
- 백그라운드60fps가 필요하면 **타이머 요청 존중 비트4만 명시하는 선택 기능**을 검토한다. 이번 자료에서 실행 속도 제한까지 끄는 비트1은 필요성이 확인되지 않았다.

아래는 결정안이며 운영 기능으로 구현하지 않았다.

|안|장점|비용·제약|
|---|---|---|
|외부 런처가 소유 PID에 명시 적용|UE C++ 변경 없이 제한된 실행에 적용 가능|PID/권한/수명 검증과 원래 상태 복원 필요, 프로세스 전체에 영향|
|TestHost의 명시적 옵션 추가, 기본 꺼짐(권장)|적용 의도가 명확하고 재현·배포가 쉬움|Windows 전용 코드·빌드/회귀 필요, 백그라운드 전력/타이머 동작 변화|
|DTCctv 플러그인에서 항상 적용|호스트마다 별도 실행기 불필요|호스트 전체 타이머 정책에 영향을 줘 플러그인 경계를 넓힘. 기본 동작으로 바로 넣지 않음|

사용자 결정은 TestHost의 기본 꺼짐 옵션(제안명 `-CctvHonorTimerResolution`, **현재 미구현**)을 추가할지다. 적용한다면 원래 정책을 저장하고 해당 프로세스의 타이머 무시 비트만 명시적으로 끈 뒤 수명 종료 시 복원해야 한다. 이번 작업은 진단 설정을 모두 되돌렸으며 운영 기본값을 변경하지 않았다.

01J의 일반 창 `package_win_16ch`는 W1 포그라운드 조건에서 기존 기준으로 PASS로 갱신한다. 이전48.4848fps FAIL은 역사 자료로 보존하고 비포커스/최소화까지 PASS로 일반화하지 않는다. PIE 종료 FAIL은 이 작업과 별개로 유지한다.
