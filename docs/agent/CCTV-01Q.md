# [Claude → Codex] CCTV-01Q Pixel Streaming 중 CUDA 장치 해제로 인코더가 깨지는 결함 수정

[CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙, [CCTV-01P](CCTV-01P.md)의 추가 규칙(다른 세션 폴더·프로세스 금지, PixelStreamingInfrastructure 읽기·실행만, 기존 FFMPEGMedia·운영 프로젝트 금지, 내 PID로만 종료)을 그대로 적용한다. 새 다운로드는 금지다(`C:\CCTV-TestPackages\tools\`의 FFmpeg·mediamtx 재사용).

## 증상 (CCTV-01P Claude 재확인)

Pixel Streaming 실행 중 TestHost에서 보기(cuda 디코딩 4채널) → 중지를 누르면:
1. `DTCctvHwDevice::ReleaseUnused`가 공유 CUDA 장치(`av_hwdevice_ctx_create(..., CUDA, nullptr, nullptr, 0)`)를 해제한다.
2. 약 60ms 뒤 PS NVENC가 `Failed to lock output bitstream [NVENC 8]`, `CUDA 700`(illegal address)로 실패한다.
3. 이후 PS 인코더 생성이 계속 실패하고(재접속해도 영상 없음), DTCctv의 새 CUDA 장치 생성도 `CUDA_ERROR_ILLEGAL_ADDRESS`로 실패한다.

증거 로그: `C:\CCTV-TestPackages\2026-11-08\Windows\DTCctvTestHost\Saved\Logs\P3c-user.log`(09:54:57 전후). 패키지 폴더 로그는 지우지 않는다.

## 할 일

1. **재현을 자동화한다.** 패키지 또는 새 Development 패키지에서 PS(`-PixelStreamingURL`, `-RenderOffScreen` 없이, 시그널링 서버 실행, 헤드리스 브라우저 없이도 인코더가 돌도록 플레이어 1개 접속이 필요하면 그 방법을 근거와 함께 고른다) + CUDA 디코딩 4채널 보기 → 중지를 명령줄·시험 훅으로 반복하고, 위 오류가 나는지 확인한다. 재현 조건(플레이어 접속 필요 여부, 해제 시점)을 기록한다.
2. **원인을 근거로 확정한다.** FFmpeg 8.1 `hwcontext_cuda.c`의 생성·해제 동작(컨텍스트 생성/pop/destroy, primary context 여부)과 UE 5.3 Pixel Streaming·AVCodecs·CUDA 모듈의 컨텍스트 사용 방식을 소스로 확인한다. 해제 자체가 원인인지, 해제 순서(디코더·프레임 풀·장치)나 스레드 문제인지 가른다.
3. **DTCctv 쪽만 고친다**(`Source/DTCctv`). 후보:
   - CUDA 장치를 해제하지 않고 모듈 종료까지 유지(ReleaseUnused에서 CUDA 제외).
   - UE CUDA 모듈과 같은 컨텍스트를 쓰거나 primary context(`AV_CUDA_USE_PRIMARY_CONTEXT`)로 생성.
   근거로 가장 단순하고 안전한 것을 고른다. 엔진·PixelStreamingInfrastructure·기존 플러그인은 바꾸지 않는다. 다른 HW 장치(d3d11va 등)의 해제 동작은 근거 없이 바꾸지 않는다.
4. 단위 테스트: 정책을 바꾸면 해당 정책 테스트를 추가한다.

## 시험

| ID | 시험 | 합격 기준 |
|---|---|---|
| Q1 | 단위 테스트 전체 | 기존 31 + 추가 통과 |
| Q2 | 수정 전 재현 | 위 증상 재현(실패 근거) 또는 재현 불가 시 조건과 근거 |
| Q3 | 수정 후 PS 반복 | PS + 플레이어 접속 상태에서 보기(cuda 4채널) 60초 → 중지 → 재시작, 10회. NVENC/CUDA 오류 0, 매 회차 PS 영상 프레임 진행(인코더 생성 실패 0), DTCctv 재시작 시 cuda 유지 |
| Q4 | 회귀 | Windows 16채널 보기 2분(01P P2 CLI와 같은 기준), Close/Open 20회 GPU 메모리 증가 없음(01Y Y6 판정 방식), 종료 시 크래시 0 |
| Q5 | Linux | 해제 정책이 Linux에도 적용되면 빌드 성공 확인. 실행은 Docker 엔진이 준비될 때만, 아니면 ENV_BLOCKED |
| Q6 | 패키지 갱신 | 수정이 들어간 Win64·Linux 패키지로 `C:\CCTV-TestPackages\2026-11-08\`의 `Windows\`·`Linux\`·zip·tar.gz를 교체(이전 것은 지우고 새 해시를 RESULT.md에 기록). 안내서의 PS 절에 `-RenderOffScreen`을 쓰지 말 것(브라우저 클릭 미반영)을 한 줄 추가 |
| Q7 | 정리 | 01Z 기준, 내가 띄운 프로세스 종료 |

## 산출물

- 의미 단위 커밋(`CCTV-01Q`). 이 지시서도 커밋한다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01Q/RESULT.md`, `result.json`, `progress.md`(수시 갱신). RESULT.md 맨 위에 사용자 재확인용 실행 명령(시그널링 서버, PS 패키지, RTSP 시험 서버 127.0.0.1 전용)을 적는다.

완료 보고: 재현 조건 / 원인과 근거 / 고친 방법 / Q1~Q7 판정과 수치 / 미수행·제한.
