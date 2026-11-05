# [Claude → Codex] CCTV-01 DTCctv V1 완성과 Windows·Linux 패키징 검증

[AGENTS.md](../../AGENTS.md)와 [DESIGN.md](../DESIGN.md)를 적용한다. ma0t10_dt의 공통 규칙은 이 저장소에 적용하지 않는다.

## 목표

운영 프로젝트가 플러그인만 넣고 `UDTCctvView` 위젯으로 CCTV를 표시할 수 있고, Windows와 Linux 패키징에서 아래 게이트를 통과한다.

## 기준

- 작업 폴더: `C:\Unreal Projects\UE-DT-CCTV-Plugin`
- branch: `feature/cctv-plugin-v1` (저장소에 커밋이 아직 없다. 첫 작업으로 현재 파일을 커밋한다)
- Claude가 작성한 V1 초안이 작업 트리에 있다. 초안의 설계는 유지하되 결함은 고친다.
- 엔진: UE 5.3, `C:\Program Files\Epic Games\UE_5.3`
- Linux 대상: RHEL 8 계열, glibc 2.28, Pixel Streaming 동시 사용
- 테스트 도구(저장소 밖, 커밋 금지): `C:\Users\ho270\AppData\Local\Temp\cctv\dl`
  - `mtx\mediamtx.exe`: RTSP 서버. 작업 폴더를 `mtx`로 두고 `mediamtx.yml`로 실행. 포트 8554
  - `ff\win\*\bin\ffmpeg.exe`: 송출 도구. h264_nvenc 사용 가능
  - 송출 예: `-re -f lavfi -i testsrc2=size=1920x1080:rate=60 -c:v h264_nvenc -preset p1 -tune ll -bf 0 -g 60 -b:v 6M -pix_fmt yuv420p -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam`
  - 한 경로를 16개 클라이언트가 함께 읽으면 16채널 시험이 된다.
- 측정 결과 폴더: `Saved/Reports/CCTV-01/` (커밋 금지)

### Claude의 현재 측정 (이 PC: RTX 4070 Ti, 8코어 16스레드, 1080p 창, Windows 에디터 -game)

| 조건 | 디코딩 | 표시 | 게임 FPS | 손실 | 비고 |
|---|---|---|---|---|---|
| NVDEC 16채널 1080p60, 공유 컨텍스트 | 27 | 26 | 70 | 큼 | 합계 약 430fps에서 막힘 |
| NVDEC 16채널, 채널별 컨텍스트 | 39 | 22 | 24 | 2514 | VRAM 5GB, 더 나쁨 |
| HW 4채널 + SW 12채널, 업로드 전 축소 적용, 1080p60 | 59.9 | 59.9 | 144 | 0 | 현재 구현 |
| 같은 조건, 1080p30 | 30 | 30 | 60 | 0 | 현재 구현 |

서버 강제 종료 22초 후 복구: 크래시 없음, 손실 0, 16채널 모두 복구(당시 동시 연결 4, 현재 기본값 8).

1080p30 16채널 3분 측정: 메모리 약 1,966MB에서 안정, 재연결 0, 손실 0. 장시간 결과는 아직 없다.

## 해야 할 일 (순서대로)

1. **기준 커밋**: 현재 파일을 의미 단위로 커밋한다. FFmpeg 바이너리는 `.gitattributes`대로 LFS로 들어가는지 확인한다. `TestHost/Plugins`는 커밋하지 않는다.
2. **Linux 링크 수정**: `RunUAT BuildCookRun -platform=Linux`가 `ld.lld: unable to find library -lavcodec.so`로 실패한다. `Linux/lib/libavcodec.so.62`처럼 버전 붙은 이름을 UBT가 잘못 처리하는 것으로 보인다.
3. **패키징**: Windows와 Linux를 각각 Development로 패키징한다. 패키지 안에 FFmpeg 라이브러리가 들어가고 플러그인 셰이더가 쿡되는지 확인한다. 패키지는 `C:\Users\ho270\AppData\Local\Temp\cctv\pkg`에 둔다.
4. **Windows 패키지 측정**: 패키지 실행 파일로 16채널 1080p60과 1080p30을 측정한다. 에디터 대신 패키지에서도 하네스가 동작해야 한다.
5. **Linux 패키지 실행**: Docker의 RHEL 8 계열 이미지로 패키지를 실행해 라이브러리 로드와 RTSP 재생을 확인한다. 이미지는 로컬의 `ps-node24-rhel8-debug:20261027` 또는 `codex-ps-cirrus:glibc251-20261027`을 읽기 전용으로 쓴다(이미지 수정·삭제 금지). 서버는 `host.docker.internal:8554`로 접속한다. GPU가 되면 NVDEC와 Vulkan까지 확인하고, 안 되면 `-nullrhi`로 소프트웨어 디코딩을 확인한다.
6. **Pixel Streaming 공존**: TestHost에서 `PixelStreaming` 플러그인을 켠 상태로 빌드하고 Linux·Windows에서 시작 시 FFmpeg 심볼 충돌이나 크래시가 없는지 확인한다. 기존 FFMPEGMedia 플러그인과 함께 넣는 구성은 시험하지 말고 README에 "함께 사용하지 말 것"으로 안내한다.
7. **크기 변경과 서브 스트림**: 실행 중 위젯 크기를 바꾸는 시나리오와 `SubUrl` 자동 전환 시나리오를 하네스에 옵션으로 추가해 시험한다. 전환 중 화면이 끊기지 않고 출력 크기가 따라가는지, 스크린샷으로 확인한다.
8. **안정성 시험**: 16채널로 `Close`/`Open` 반복과 서버 재시작을 수행하고 메모리, 핸들·스레드 수, GPU 메모리를 비교한다. 그다음 1080p60 16채널로 최소 60분 연속 재생한다. 처음 10분을 뺀 메모리 증가율과 스레드 수 변화를 result에 기록한다. 누수가 있으면 원인을 찾아 고친다.
9. **독립 검수 대비 코드 점검**: 스레드 경합, 수명, 종료 순서, 오류 경로를 스스로 재검토한다. 특히 `UDTCctvStream`이 렌더 명령 대기 중 파괴될 때, `FDTCctvSession` 종료, 해상도 변경, `ActiveHwSessions` 집계를 확인한다.
10. **문서**: `README.md`를 쓴다. 운영 프로젝트 적용 방법(플러그인 복사, `.uproject` 활성화, Git LFS), UMG 사용법(`DT CCTV` 팔레트의 `DTCctvView`), C++·블루프린트 API, 설정값, 카메라 코덱 확인 방법(로그 `코덱=... 해상도=... FPS=...`), 권장 구성(서브 스트림), 한계를 넣는다. 측정 결과에 맞춰 `docs/DESIGN.md`를 갱신한다.

## 범위

- 허용: 이 저장소 전체. Editor, 빌드, GPU, 측정, Docker(위 이미지 실행) 허용.
- 금지: 다른 프로젝트 폴더 수정, 이미지·컨테이너 외 시스템 설정 변경, FFmpeg 버전 교체, 추가 외부 다운로드.
- 커밋: 허용(의미 단위, 메시지에 `CCTV-01`). push, PR, merge: 금지(Claude가 검수 후 진행).
- 공개 API(`UDTCctvStream`, `UDTCctvView`, `UDTCctvSubsystem`, `FDTCctvStreamConfig`)는 필요한 만큼 추가할 수 있으나, 이름 변경·삭제는 결과에 기록한다.
- 측정 후 직접 띄운 mediamtx, ffmpeg, Editor, 컨테이너를 모두 종료한다.

## 예비안

| 차단 조건 | 묻지 않고 적용할 예비안 | 검증·보고 |
|---|---|---|
| Linux에서 버전 이름 `.so.62` 링크 불가 | 같은 폴더에 내용이 `INPUT(libavcodec.so.62)`인 텍스트 링커 스크립트 `libavcodec.so`를 두고 이것을 링크. 안 되면 실제 파일을 `.so` 이름으로 복사 | `readelf -d`로 NEEDED가 `.so.62`인지, 실행 시 로드되는지 확인 |
| 패키지에서 런타임 로드 실패 | `FPlatformProcess::GetDllHandle`로 모듈 시작 시 명시적 로드, `PublicDelayLoadDLLs` 사용 | 경로와 근거 기록 |
| Docker에서 GPU·Vulkan 불가 | `-nullrhi`로 세션과 디코딩만 검증 | 렌더링은 미수행으로 기록하고 사용자 서버 확인 항목에 넣음 |
| Docker 컨테이너에서 host 접속 불가 | 같은 컨테이너에 mediamtx Linux 바이너리가 없으면 `--add-host` 시도 후, 안 되면 Linux 재생은 라이브러리 로드까지만 검증 | 미수행 범위 기록 |
| 패키지 측정에서 하드웨어 4채널 설정이 과부하 | `MaxHardwareDecodeStreams` 기본값 조정 근거 측정 | 측정값 기록 |
| Pixel Streaming과 Linux 심볼 충돌 | `-Bsymbolic` 등 링크 옵션 또는 명시적 로드 격리 검토, 안 되면 재현 절차와 함께 즉시 보고 | 결정 필요로 표시 |
| 60분 연속 실행이 다른 항목을 막음 | 맨 마지막에 실행 | 실제 실행 시간을 사실대로 기록 |

예비안으로 해결할 수 없는 공개 계약 변경, FFmpeg 교체 필요, 합격 기준 미달은 근거와 선택지를 즉시 보고하고 해당 항목만 멈춘 뒤 다음 항목을 진행한다.

## 게이트

| gate | 기준 |
|---|---|
| unit | `Automation RunTests DTCctv` 전부 성공, 실패 0 |
| build_win | Editor와 Game 대상 Windows 빌드 성공 |
| build_linux | Game 대상 Linux 빌드와 패키징 성공 |
| package_win_16ch | 패키지 16채널 1080p60: 디코딩 평균 ≥ 58fps, 표시 평균 ≥ 58fps, `PacketsDropped` 0, 재연결 0, 60초 이상 |
| package_linux_run | RHEL 8 계열에서 실행, FFmpeg 로드, RTSP 프레임 디코딩 확인 |
| fault | 서버 재시작 후 크래시 없이 전 채널 복구, 복구 시간 기록 (서버 복귀 후 5초 이내 목표) |
| resize_substream | 크기 변경과 서브 스트림 전환에서 크래시 없음, 스크린샷 확인 |
| soak | 60분 연속, 재연결 0, 손실 0, 10분 이후 메모리 증가율 기록, 스레드 수 일정 |
| ps_coexist | PixelStreaming 켠 빌드에서 시작과 16채널 재생 성공 |

기대값을 낮추지 않는다. 이 PC에서 달성하지 못하면 FAIL로 기록하고 원인과 측정값을 쓴다.

## 산출물

- 의미 단위 커밋, `README.md`, 갱신된 `docs/DESIGN.md`
- `Saved/Reports/CCTV-01/RESULT.md`: 항목별 결과, 측정 조건과 수치, 미수행, 한계, 결정 필요
- `Saved/Reports/CCTV-01/result.json`: `task_id`, `branch`, `commits`, `base_commit`, `gates`(gate별 status/note), `measurements`, `not_done`, `known_limits`, `decisions_needed`, `artifacts`, `secrets_exposed`, `secrets_note`
- 최종 스크린샷(PNG)과 측정 CSV 요약. 패키지 사본과 대용량 로그는 결과 폴더에 넣지 않는다.

## 보고

완료 보고 형식: 항목 / 커밋(순서대로) / 변경 요약 / 게이트별 결과와 수치 / 미수행·제한 / 결정 필요.
