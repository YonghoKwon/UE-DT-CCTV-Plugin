# CCTV-01G 코덱 검증

Ryzen 7 7800X3D(8코어 16스레드), RTX 4070 Ti 12GiB, Windows와 로컬 RHEL 8.10 컨테이너에서 합성 파일을 반복 송출한 결과입니다. 한 RTSP 경로를 16클라이언트가 읽었습니다. 미리 인코딩한 파일을 복사 송출했고, 측정 중 GPU 인코더 사용률 0%를 확인했습니다.

전체 평균과 각 채널의 FPS가 원본의 97% 이상이어야 합니다. 손실·오류·재연결은 0, Windows 대체율은 1% 이하, 채널별 frame age p95는 30fps 입력에서 70ms 이하, 60fps 입력에서 50ms 이하가 기준입니다. Windows PNG는 직접 열어 판정했습니다.

## 코덱별 결과

Windows 열은 decode/display fps이며, Linux 열은 decode fps입니다. Linux nullrhi에서는 표시·색·PNG·age를 검증하지 못했습니다.

| 조건 | Windows | Linux SW16 | Linux Auto 최대HW4 |
|---|---|---|---|
| C1 H264 High 1080p30 B2 GOP2초 | PASS 29.989/29.981, p95 20ms | PASS 30.011 / 최저 30.000 | PASS 30.006 / cuda4,software12 |
| C2 H264 High 1080p30 GOP12초 | PASS 29.977/29.978, p95 19ms | PASS 30.011 / 최저 29.999 | PASS 30.000 / software12,cuda4 |
| C3 H265 Main 1080p30 B2 GOP2초 | PASS 29.969/29.978, p95 21ms | PASS 29.978 / 최저 29.996 | PASS 30.011 / software12,cuda4 |
| C4 H265 Main 1080p60 GOP2초 | PASS 59.989/59.589, p95 36ms | PASS 59.985 / 최저 59.990 | PASS 59.985 / cuda4,software12 |
| C5 H265 Main 1080p30 GOP12초 | PASS 29.976/29.975, p95 21ms | PASS 30.009 / 최저 29.988 | PASS 30.011 / cuda4,software12 |
| C6 H265 Main10 1080p30 | PASS 30.025/30.022, p95 22ms | PASS 29.978 / 최저 29.993 | PASS 29.979 / cuda4,software12 |
| C7 H265 Main 4K30 | PASS 30.014/30.019, p95 31ms | FAIL 29.599 / 최저 27.829 | PASS 30.001 / software12,cuda4 |
| C8 MJPEG 1080p30 4:2:2 | FAIL 29.724/29.793, p95 73ms | FAIL 27.756 / 최저 22.136 | FAIL 27.119 / software16 |
| C9 H265 4K 메인/H264 640 서브 | PASS 30.032/30.028, p95 43ms | PASS 30.060 / 최저 29.997 | PASS 30.011 / cuda4,software12 |

C1~C7·C9의 정상 협상 기본 자료는 F 최종 C++ `95cd411`입니다. 실패한 형식 협상을 수정한 `9c98d85`에서는 영향을 받는 MJPEG를 새 패키지로 재측정하고, 지연 4조건·정지·패키지·장애·소크를 별도로 회귀합니다. 각 수치의 코드 버전과 원자료는 `Saved/Reports/CCTV-01G/codec-matrix.json`에 있습니다. 이전 자료를 새 코드의 새 측정으로 표시하지 않습니다.

C7 Linux SW16 기본 설정의 최저 채널 27.829fps는 FAIL로 보존했습니다. 실제 틱 60·`SoftwareDecodeThreads=1` 대조에서는 채널별 97%까지 통과했습니다. 같은 스레드 1 설정의 CUDA 혼합은 미달이어서 전체 기본값을 바꾸지 않았습니다. CUDA4/SW12는 스레드 2 대조가 통과했습니다.

이 장치·라이브러리·MJPEG 4:2:2 시료에서는 CUDA 형식 초기화가 실패해 실제 SW16으로 폴백했습니다. HW4/SW12 성공으로 표시하지 않습니다. 정상 SW 형식이 제공됐는데도 RTSP 재접속을 요청하던 문제는 고쳐 재연결 11회→0회를 확인했습니다. 사용 가능한 형식이 없으면 실패 신호를 유지하며, 같은 연결에서 실패한 HW 협상을 반복하지 않습니다.

MJPEG 수정 후 Windows p95는 73ms로 70ms 기준을 넘었습니다. 스레드 1 대조는 81ms였고, Linux도 틱 60·스레드 수 대조 후 97% 기준에 미달했습니다. 단일 파일 진단에서 NV12 변환을 포함한 CPU 비용이 증가했으나, 16 RTSP의 전달·스케줄링·큐 대기까지 완전히 분해한 원인 확정과는 구분합니다.

## 지원 구성과 권장 설정

포함된 Windows DLL·Linux SO의 h264/hevc/mjpeg/mpeg4/vp9/av1 디코더를 직접 조회했습니다. AV1 기본 디코더는 libdav1d입니다. MPEG4·VP9·AV1은 구성만 확인했고 실시간 행렬은 미수행입니다. Windows CUDA/D3D11VA와 RHEL 8 CUDA에서 실제 Main10 디코딩과 P010 다운로드를 확인했습니다. 플러그인은 P010·4:2:2를 swscale NV12로 변환하며, HDR 톤매핑 검증은 포함하지 않습니다.

- 통과한 입력 설정을 기준으로 1080p30 H264 High 또는 H265 Main, 8비트 4:2:0, GOP 2초를 시작점으로 권장합니다. H264/H265 B2도 검증했습니다. 60fps와 Main10은 위 표의 성공 범위와 CPU 부하를 확인해야 합니다.
- GOP 12초는 새 연결의 첫 렌더 등록·재생에 약 8~9초가 걸린 표본이 있습니다. 빠른 첫 화면이 필요하면 GOP 2초와 상시 연결을 권장합니다. 벤더 H264+/H265+ 전체 동작을 검증한 것은 아닙니다.
- 작은 뷰는 H264 640×360 서브, 큰 뷰만 H265 4K 메인으로 쓰는 C9 구성이 통과했습니다. 실제 카메라 주소는 운영 프로젝트의 `SubUrl`에 넣습니다.
- 하드웨어 예산은 프로젝트 설정 `Game → DT CCTV → MaxHardwareDecodeStreams` 또는 `Config/DefaultGame.ini`의 `[/Script/DTCctv.DTCctvSettings]`에서 같은 키로 조정합니다. H265 기본 4채널 혼합 경로가 통과해 전역 기본값 4를 유지했습니다. 숫자를 올려도 미지원 MJPEG 형식이 HW 가능으로 바뀌지는 않습니다.
- 소프트웨어 스레드는 같은 설정의 `SoftwareDecodeThreads`입니다. C7 Linux SW 전용에서 틱 60·스레드 1을 조건부 권장하며, 혼합/H264 전체에 일괄 적용하지 않습니다.
- MJPEG 16채널 30fps는 본 PC에서 미달하므로 별도 서버·입력 설정 검증이 필요합니다. 해상도·FPS를 낮춘 새 조건이나 카메라 H264 대안을 원래 MJPEG 조건의 PASS로 간주하지 않습니다.

운영 서버 사양이 제공되지 않아 확정 성능을 예측하지 않았습니다. 16×4K30은 약 3.981Gpixel/s입니다. 97% 기준 전체 465.6fps와 각 채널 29.1fps를 동시에 만족하는 실측 처리량이 필요합니다. 실제 카메라 복잡도·네트워크·Vulkan 렌더링·동시 Pixel Streaming 부하는 별도 수용 항목입니다.

## Linux 운영 점검

기존 인자 없는 GPU/60fps 점검과 `--software-only` 동작을 보존했습니다. 코덱 옵션을 지정하면 100초 실행, 처음 10초 제외 후 80초 이상, 원본 97%, 채널별 FPS, 코덱 일치, 오류 0을 판정합니다.

```bash
TestHost/Scripts/linux_server_check.sh /path/to/package "$CAMERA_URL" --software-only --codec hevc --fps 30
TestHost/Scripts/linux_server_check.sh /path/to/package "$CAMERA_URL" --decode-only --codec hevc --fps 30
```

`--decode-only`는 nullrhi Auto CUDA4/SW12 범위이며 렌더링 성공을 뜻하지 않습니다. 두 옵션은 Main10 시료로 실제 실행해 통과했습니다. 기존 60fps 기본 분석 호환과 잘못된 코덱 거부도 검증했습니다. URL은 환경변수 경로로 전달하고 로그는 가립니다. 원격 실 GPU 서버의 Vulkan은 본 작업에서 ENV_BLOCKED를 계승합니다.
