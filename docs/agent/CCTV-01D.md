# [Claude → Codex] CCTV-01D 미충족 항목 원인 파악과 수정

사용자 요청: 모든 항목을 만족하도록 원인을 파악하고 수정한다. [CCTV-01C](CCTV-01C.md)의 절대 금지, 다른 작업과의 공존, 직렬 측정 규칙을 그대로 적용한다. 합격 기준을 낮추지 않는다.

## Claude가 확인한 사실

`linux-gpu-a1`은 플러그인 로드 전에 끝났다.

- `vpCreateInstance` 실패, `VK_ERROR_INCOMPATIBLE_DRIVER`, 호출 위치는 `CheckVulkanProfile` → `RHIInit`. 이어서 Signal 11, 종료 코드 139.
- `LD_LIBRARY_PATH`에 `/usr/lib/wsl/lib`와 `/opt/mesa-dzn/lib64`가 있다. 이 PC의 Docker는 WSL2 기반이고, 컨테이너의 Vulkan이 dzn 드라이버뿐일 가능성이 높다. 가설이므로 직접 확인한다.
- 소프트웨어 실행(`-nullrhi -CctvSoftware`)은 하드웨어 디코딩과 렌더링을 쓰지 않으므로, Linux의 NVDEC와 YUV 렌더링은 아직 미검증이다.

## 수정과 검증 항목

### D1. Linux NVDEC 검증

`--gpus all -nullrhi`로 `-CctvSoftware` 없이 16채널을 실행한다.

- 기준: `decoder=cuda` 확인, 전 채널 Playing, 디코딩 프레임 증가, 채널당 디코딩 FPS 기록. `MaxHardwareDecodeStreams` 초과 채널의 소프트웨어 전환도 확인한다.
- 실패하면 `libnvcuvid`와 `libnvidia-encode`의 컨테이너 내 존재, `av_hwdevice_ctx_create` 오류를 근거로 원인을 구분한다. 플러그인 결함이면 고치고, 하드웨어 실패 시 소프트웨어로 전환되는지도 확인한다.

### D2. Linux 렌더링 검증

원인을 먼저 확인한다. 컨테이너에서 ICD 목록, 드라이버 파일, `vulkaninfo`(있을 때)를 수집한다.

다음 순서로 시도하고, 렌더링 결과는 PNG로 확인한다.

1. 이미 있는 드라이버(로컬 이미지, WSL 안의 lavapipe 등)로 Vulkan 실행. 이미지 다운로드는 금지다.
2. 엔진이 제공하는 프로파일 검사 우회 옵션이 있으면 시험용으로만 사용한다. 쓰면 그 사실을 결과에 적는다.
3. 위가 모두 불가능하면 이 PC에서 입증할 수 없는 것으로 판정한다. 게이트는 `ENV_BLOCKED`로 적고, 수집한 근거를 함께 남긴다. PASS로 적지 않는다.

`ENV_BLOCKED`이면 운영 Linux GPU 서버에서 사용자가 바로 실행할 `TestHost/Scripts/linux_server_check.sh`를 만든다.

- 입력은 패키지 경로와 RTSP URL이다. 인증 정보를 로그에 남기지 않는다.
- Vulkan 실행, NVDEC 사용, FPS, 손실, 재연결, 스크린샷을 수집해 PASS 또는 FAIL을 출력한다.
- 이 PC에서는 구문 검사와 가능한 범위의 소프트웨어 실행까지만 확인하고, 미실행 부분을 적는다.

### D3. 플러그인 쪽 방어

Vulkan 초기화 실패는 엔진 단계에서 끝나므로 플러그인이 막을 수 없다. README의 Linux 절에 필요한 드라이버 조건과 확인 방법을 적는다. 플러그인이 `-nullrhi`나 렌더링 불가 환경에서 충돌하지 않는지는 D1로 확인한다.

### D4. 판정 정리

`Linux GPU FAIL`처럼 환경과 코드 원인이 섞인 결과는 근거와 함께 구분해 기록한다. 추가로 다음을 확인한다.

- `close_open`의 decode 48fps: 닫힌 시간을 뺀 재생 구간 FPS로 다시 계산한다. 재생 구간도 기준에 못 미치면 재연결 지연을 조사해 고친다. 메모리, 핸들, 스레드가 반복 전후로 회복하는지 판정한다.
- `resize_extremes`의 확대 직후 흐림: 지속 시간을 측정한다. 저해상도 프레임이 1초를 넘게 이어지면 원인을 조사하고 고친다.
- 결과 설명의 깨진 한글을 고친다. 수치는 바꾸지 않는다.
- 아직 `REVIEW_REQUIRED`인 항목은 모두 PASS 또는 FAIL로 확정한다.

### D4-1. soak 스레드 변동 원인 규명

Claude가 `soak-a1/resources.csv`를 독립 확인한 결과, 스레드가 121~123 사이를 약 4~5분 주기로 오르내리고, 스레드 1개당 핸들이 2개씩 함께 변한다. 총수는 늘어나지 않는다. 플러그인 세션 스레드가 아니라 스레드 풀의 생성과 소멸일 가능성이 있다. 가설이므로 직접 확인한다.

- 스레드 이름이 비어 있어 이전 조회가 실패했다. 스레드별 시작 주소와 소속 모듈(`QuerySetWin32StartAddress`, 모듈 범위 대조 등)로 구분하고, 필요하면 플러그인 세션이 소유한 스레드 수를 하네스 CSV에 기록한다.
- 스레드 변동이 3주기 이상 들어가도록 16채널로 20분 측정한다. 원인 파악이 끝나기 전에는 60분을 반복하지 않는다.
- 변동이 플러그인 소유 스레드나 FFmpeg에서 나오면 결함으로 보고 고친다.
- 플러그인과 FFmpeg 스레드가 일정하고 변동이 OS나 엔진에서만 나오면, 게이트는 FAIL로 유지하고 근거를 정리한 뒤 판정 기준 변경 여부를 `결정 필요`로 보고한다. 기준은 스스로 바꾸지 않는다.
- 같은 측정에서 메모리 추세(Private +5.3MiB/h 회귀)가 표본 변동인지도 함께 판단하고 근거를 적는다.

### D4-2. FramesSuperseded 4건 원인

60분 동안 표시 프레임이 4건 교체됐다(`FramesSuperseded=4`, `PacketsDropped=0`). 사용자 요구는 프레임 손실 없음이다. 발생 시각과 `max_frame_ms`, 큐 길이를 대조해 원인을 찾는다. 대기열 제한이나 렌더 지연처럼 플러그인에서 고칠 수 있으면 고치고, 고칠 수 없으면 수치와 이유를 사용자 확인 항목에 넣는다. 지표 이름이나 집계 방식을 바꿔 숨기지 않는다.

### D5. 실패 항목 처리

CCTV-01C의 어떤 게이트라도 FAIL이면 이 지시서에서 원인을 찾아 고친다(soak 포함). 게이트당 수정 시도는 3회까지이고, 그래도 미달이면 수치와 병목을 기록한다.

### D6. 재측정 범위

- 공용 C++를 고치면 빌드, 단위 테스트, 패키징과 package_win_16ch, fault, close_open, soak를 다시 한다.
- Linux 전용 코드나 스크립트, 문서만 고쳤다면 Windows 게이트는 다시 하지 않고 이유를 적는다.
- 고친 내용에 맞는 단위 테스트를 가능한 범위에서 추가한다.

## 산출물

- 의미 단위 커밋(`CCTV-01D`). 이 지시서도 커밋한다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01D/RESULT.md`, `result.json`, `progress.md`. 형식은 CCTV-01C와 같다.
- 게이트별 최종 판정은 PASS, FAIL, ENV_BLOCKED, NOT_RUN 중 하나다. 이 PC에서 입증되지 않은 것을 PASS로 적지 않는다.

완료 보고: 항목 / 커밋 / 게이트별 판정과 수치 / 미수행·제한 / 결정 필요.
