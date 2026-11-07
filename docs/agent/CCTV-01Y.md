# [Claude → Codex] CCTV-01Y FFmpeg 실행 중 로드로 기존 플러그인과 호출 분리

사용자 결정(2026-11-07): CCTV-01V에서 찾은 함수 섞임을 DTCctv 쪽 수정으로 해결하고, 두 플러그인 동시 재생까지 시험한다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙을 그대로 적용한다.

**기존 FFMPEGMedia 플러그인은 절대 고치지 않는다.** 소스, 빌드 규칙, 바이너리, descriptor 모두 그대로 둔다(사용자 지시). 시험에는 `Temp\cctv\ffm-main`의 main `819e004`를 원본 해시 그대로 복사해 쓰고, TestHost에서 켜는 것만 시험 중에 허용한다. 동봉 FFmpeg 8.1 바이너리와 SONAME, 해시도 바꾸지 않는다.

## 배경 (CCTV-01V)

monolithic 패키지에서는 두 플러그인의 같은 이름 FFmpeg 함수(`avformat_open_input` 등)가 하나의 import로 합쳐진다. 기존 플러그인 호출 일부가 FFmpeg 8.1로 가고 구 전용 함수는 구 라이브러리로 가서, 동시 재생 시 Windows·Linux 모두 충돌했다. Linux 링크 이름 분리(ce43331)는 링크 실패만 고쳤다.

## 설계: 실행 중 로드(함수 표)

01V 보고의 "별도 공유 브리지" 대신, 추가 빌드 산출물이 없는 실행 중 로드를 기본안으로 한다. 기본안이 근거 있게 불가능하면 이유를 보고하고 멈춘다(임의로 다른 구조로 바꾸지 않는다).

1. **링크 제거:** `DTCctvFFmpeg.Build.cs`에서 Windows `.lib`와 Linux 링크 스크립트를 `PublicAdditionalLibraries`에서 뺀다. 헤더 경로와 `RuntimeDependencies` 스테이징(파일 이름 그대로)은 유지한다. 필요 없어진 `libdtcctv_*.so` 링크 스크립트와 Windows `.lib`는 지우되, 지워도 되는지 근거를 남긴다.
2. **함수 표:** DTCctv가 쓰는 FFmpeg 함수 전부(약 50개, 헤더의 인라인 함수·매크로가 내부에서 부르는 함수 포함, 예: `av_err2str` → `av_strerror`)를 함수 포인터 표 하나로 모은다. DTCctv 코드의 모든 FFmpeg 호출은 이 표를 거친다. 함수 이름 목록은 소스 검색이 아니라 **현재 오브젝트의 미해결 심볼 목록**으로 확정한다.
3. **로드:** 모듈 시작 시(또는 첫 Open 전에 한 번, 게임 스레드 밖 경합 없이) 스테이징 폴더의 정확한 전체 경로로 의존 순서대로 연다: avutil → swresample → swscale → avcodec → avformat.
   - Windows: `LoadLibraryExW`(전체 경로, 의존 DLL이 같은 폴더에서 찾아지게) + `GetProcAddress`. 에디터(모듈 Binaries)와 패키지(실행 파일 옆) 경로를 모두 처리한다.
   - Linux: `dlopen`(전체 경로, `RTLD_NOW | RTLD_LOCAL`) + `dlsym`. `RTLD_DEEPBIND`가 필요한지는 실제 결합 결과로 판단하고, 쓰면 이유를 적는다.
   - 신 라이브러리 내부의 상호 참조(avformat → avcodec)가 신 계열에 결합되는지 확인한다.
4. **실패 처리:** 파일이나 함수가 하나라도 없으면 크래시 없이 모듈은 살아 있고, 스트림은 명확한 오류(어느 파일·함수인지, 계정 정보 없이)로 끝난다. `LogDTCctv`와 DTCore 로그에 남긴다. 부분 로드 상태로 재생을 시도하지 않는다.
5. **종료:** 모든 세션이 끝난 뒤 해제한다. 해제 순서와 시점 때문에 종료 크래시가 생기면 해제하지 않고 프로세스 종료에 맡기는 쪽을 택하고 근거를 적는다.
6. 성능 영향은 없어야 한다(함수 포인터 호출).

## 시험

측정은 직렬로 하고, 다른 UE 작업이 있으면 기다린다. 프로세스는 내가 띄운 PID로만 종료한다. Windows 일반 창 측정 전에는 `OpenInputDesktop`으로 잠금 해제를 확인한다.

| ID | 시험 | 합격 기준 |
|---|---|---|
| Y1 | 정적 확인 | DTCctv 단독·공존 각각 Win64 패키지 PE와 Linux ELF에 **FFmpeg 62 계열 import·DT_NEEDED가 없음**. 에디터 DTCctv 모듈 DLL도 같음. DTCctv 오브젝트에 FFmpeg 미해결 심볼 0 |
| Y2 | 단위 테스트 전체 + 로드 실패 테스트 | 기존 단위 전부 통과. 함수 누락·파일 누락을 흉내 낸 테스트에서 크래시 없이 오류 상태 |
| Y3 | 로드 실패 실제 확인 | 패키지 복사본에서 `avcodec-62` 하나를 치운 뒤 실행: 크래시 없음, 오류 로그 확인 |
| Y4 | DTCctv 단독 회귀 | Win 16채널 1080p60(01V V6와 같은 조건), Linux SW·자동 16채널. 수치가 V6와 오차 범위 안 |
| Y5 | 코덱·경로 회귀 | C1, C3, C6, C7, C8, H60 각 4채널 60초를 Windows(자동=NVDEC·D3D11VA 포함)와 Linux(SW, 자동 CUDA)에서. 함수 표의 HW·스케일러·MJPEG 경로를 모두 지나야 함 |
| Y6 | 장애·수명 회귀 | 송출 중단 후 복구(재연결), Close/Open 20회, 서브 전환 1회. 01F와 같은 판정 방식으로 짧게 |
| Y7 | 공존, 기존 미사용 | Win·Linux 16채널, V2·V5와 같은 기준 |
| Y8 | **기존 플러그인만 재생, DTCctv 설치됨** | 기존 플레이어 4채널 120초, Win·Linux. 크래시 0, 프레임 진행. 사용자가 걱정한 "설치만 해도 영향" 조건 |
| Y9 | **두 플러그인 동시 재생** | 기존 4 + DTCctv 12, Win·Linux 각 120초 이상. 크래시·교착 0, DTCctv 채널은 V2 기준, 기존 채널은 프레임 진행 기록 |
| Y10 | 실행 중 결합 | Linux `LD_DEBUG=bindings`와 maps로 기존 플러그인 호출은 58 계열, DTCctv 호출은 62 계열에만 결합(섞임 0). Windows는 모듈 목록과 호출 경로로 같은 수준의 근거 |
| Y11 | 에디터 확인 | 두 플러그인 활성 Editor 빌드, PIE에서 DTCctv 4채널과 기존 1채널 재생 1회(짧게) |
| Y12 | 정리 | 01Z 기준. TestHost descriptor 원복, 큰 산출물 삭제, 요약·대표 증거 보존 |

- 기존 플레이어 자체 결함(예: `avdevice-58.dll` 오류 126)은 결함으로만 기록하고 고치지 않는다. 기존 플레이어가 Linux nullrhi에서 동작하지 않으면 그 근거를 남기고 해당 Linux 항목만 ENV_BLOCKED로 한다.
- Y9가 실패하면 원인을 근거와 함께 보고하고 멈춘다. 기준을 낮추거나 기존 플러그인을 고치지 않는다.

## 문서

- `docs/FFMPEG_MEDIA_COEXISTENCE.md`에 01Y 결과를 맨 위에 적는다(이전 기록 보존).
- `docs/INTEGRATION_GUIDE.md` 10절, README 공존 문단, `Source/ThirdParty/DTCctvFFmpeg/README.md`(링크 방식·파일 목록)를 결과에 맞게 고친다. 통과하면 "함께 설치·동시 재생 가능"과 기존 플러그인 제거 순서를, 실패하면 현재 제한을 적는다. 01V의 `PlatformDenyList` 안내는 통과 시 "필요 없음"으로 바꾼다.
- 운영 프로젝트는 읽기만 한다.

## 산출물

- 의미 단위 커밋(`CCTV-01Y`). 이 지시서도 커밋한다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01Y/RESULT.md`, `result.json`, `progress.md`(수시 갱신).

완료 보고: 함수 표 범위와 로드 방식 / Y1~Y12 판정과 수치 / 동시 재생 결과 / 미수행·제한 / 결정 필요.
