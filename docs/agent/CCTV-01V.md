# [Claude → Codex] CCTV-01V 기존 FFMPEGMedia와 Linux 공존 빌드 수정

사용자 요청: 기존 FFMPEGMedia 플러그인과 DTCctv를 **둘 다 켠 상태**로 Windows·Linux에서 빌드·패키징·시험할 수 있게 한다. 사용자는 공존 상태로 먼저 운영 시험을 하고, 나중에 기존 플러그인을 제거할 계획이다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙(다른 UE 작업 대기, 프로세스 이름으로 종료 금지, 운영 프로젝트 읽기 전용, 비밀값 금지, 승인 없는 다운로드 금지, 기존 Docker 이미지·컨테이너 변경 금지)을 그대로 적용한다.

## 배경 (CCTV-01I, R15)

- 두 플러그인을 함께 켠 Linux 패키징이 링크 단계에서 실패했다. 기존 플러그인 오브젝트가 요청한 구 API 4개(`av_format_inject_global_side_data`, `av_get_channel_layout_nb_channels`, `av_get_default_channel_layout`, `swr_alloc_set_opts`)가 미해결이었다.
- 원인 판단: 두 플러그인이 같은 `-lavcodec` 등의 링크 이름을 쓰고, 검색 경로에서 DTCctv 폴더가 먼저 와서 기존 플러그인의 요청도 DTCctv의 `libavcodec.so`(FFmpeg 8.1을 가리키는 링커 스크립트)로 연결됐다.
- 기존 플러그인 단독 Linux 링크는 성공한다. Windows는 DLL 이름이 달라(`avcodec-58` / `avcodec-62`) 공존 빌드와 DTCctv 재생이 통과했다.
- 현재 규칙: `Source/ThirdParty/DTCctvFFmpeg/DTCctvFFmpeg.Build.cs`는 UBT가 버전 접미사 `.so`를 `-l` 이름으로 잘못 바꾸는 문제를 피하려고 무버전 링커 스크립트(`libavcodec.so` → `INPUT(libavcodec.so.62)`)를 `PublicAdditionalLibraries`에 넣는다.

## 수정 원칙

- **DTCctv 쪽만 고친다.** 기존 플러그인(시험본 main `819e004`)의 소스·빌드 규칙·바이너리는 바꾸지 않는다. 나중에 제거할 플러그인이고 운영 설치본과 같은 상태를 유지해야 한다.
- 기존 플러그인의 `-lavcodec` 등이 DTCctv 폴더에서 아무것도 찾지 못하게 하는 것이 목표다. FFmpeg 8.1 바이너리 자체와 SONAME(`libavcodec.so.62` 등), 런타임 스테이징 이름은 바꾸지 않는다.
- 먼저 UE 5.3 엔진 소스(`LinuxToolChain.cs` 등)에서 `PublicAdditionalLibraries`, `PublicSystemLibraries`, `PublicSystemLibraryPaths`가 링크 인자로 어떻게 바뀌는지 확인하고, 그 근거로 방법을 고른다. 후보:
  1. 링커 스크립트 파일 이름을 DTCctv 고유 이름으로 바꾼다(예: `libdtcctv_avcodec.so` → `INPUT(libavcodec.so.62)`). 링크 이름이 `-ldtcctv_avcodec`가 되고, DTCctv 폴더에는 `libavcodec.so`가 더 이상 없다. `INPUT`의 파일 탐색이 실제로 되는지 확인한다.
  2. `-l:libavcodec.so.62`처럼 정확한 파일 이름 지정(UBT가 그대로 전달하는 경우에 한해).
  3. 버전 파일의 전체 경로 직접 지정(UBT가 `-l`로 바꾸지 않는 경우에 한해).
  위 후보 외 방법을 써도 되지만, 엔진 소스 근거와 생성된 실제 링크 명령(RSP)으로 확인한다. 엔진 소스는 바꾸지 않는다.
- 런타임에서 두 FFmpeg(구 58 계열, 신 62 계열)이 한 프로세스에 함께 올라올 때 심볼이 섞이지 않는지 확인한다. 각 라이브러리의 SONAME과 심볼 버전(`LIBAVCODEC_58` / `LIBAVCODEC_62` 등)이 서로 다른지, 실제 실행에서 DTCctv 호출은 신 라이브러리, 기존 플러그인 호출은 구 라이브러리에 결합되는지 `LD_DEBUG=bindings` 또는 같은 수준의 근거로 확인한다. 섞이면 그 원인과 대안을 보고하고 임의로 우회하지 않는다.
- 기존 동작이 바뀌면 안 된다. DTCctv 단독(기존 플러그인 없음) Windows·Linux 빌드와 재생이 그대로 통과해야 한다.

## 시험

기존 플러그인 시험본은 `main 819e004`를 쓴다(`feature/dt-project-sync`는 빌드되지 않는다). Temp에 남겨 둔 clone(`Temp\cctv\ffm-main`)을 쓰고, 원본은 보존한 채 시험 복사본을 만든다. TestHost에 기존 플러그인을 넣는 변경은 시험 중에만 하고 끝나면 원래대로 되돌린다(시험 하네스 코드 추가는 아래 V4 범위에서 허용).

| ID | 시험 | 합격 기준 |
|---|---|---|
| V1 | 두 플러그인 활성 Linux 빌드·패키징 | 종료 0. 링크 RSP에서 DTCctv와 기존 플러그인이 서로 다른 라이브러리를 요청. ELF `DT_NEEDED`에 58 계열과 62 계열이 함께 있음 |
| V2 | V1 패키지, 기존 플러그인 켜 두고 미사용, DTCctv 16채널 Linux nullrhi(SW, 자동 각각) | 01G·01L과 같은 기준: 전 채널 Playing, 손실·재연결·오류 0, 디코딩 FPS ≥ 97% |
| V3 | 런타임 심볼 결합 | `/proc/<pid>/maps`에 두 계열이 각자 경로로 로드. DTCctv 쪽 FFmpeg 호출이 62 계열에 결합. 섞임 0 |
| V4 | **두 플러그인 동시 재생** (사용자 요청의 핵심) | TestHost에 시험 전용 경로를 추가해 기존 플러그인(UE Media Framework `MediaPlayer` + FFMPEGMedia)으로 4채널, DTCctv로 12채널을 동시에 재생한다. Windows 패키지(일반 창 포그라운드 또는 오프스크린, 사용한 조건 기록)와 Linux 패키지(nullrhi, 기존 플레이어가 nullrhi에서 동작하지 않으면 그 근거를 남기고 ENV_BLOCKED). 각 120초 이상. DTCctv 채널은 V2 기준. 기존 플레이어 채널은 재생 상태와 프레임 진행을 기록(기준은 참고용, 기존 플러그인 자체 결함은 결함으로만 기록). 크래시·교착 0 |
| V5 | Windows 공존 회귀 | 두 플러그인 활성 Win64 패키징 종료 0, DTCctv 16채널 01I와 같은 기준 |
| V6 | DTCctv 단독 회귀 | 기존 플러그인 없이 Win64·Linux 빌드·패키징 종료 0, 16채널 짧은 재생(90초) 통과, 단위 테스트 전체 통과. Linux 패키지에 SONAME 5개가 그대로 스테이징 |
| V7 | 정리 | 시험 복사본·임시 패키지·원자료는 Saved 아래 또는 Temp에 두고, 작업 끝에 01Z 기준으로 불필요한 대용량 산출물을 지운다(요약과 대표 증거는 보존). TestHost descriptor 원복 확인 |

- 측정은 직렬로 한다. Windows 일반 창 측정 전에는 `OpenInputDesktop`으로 잠금 해제를 확인하고, 잠겨 있으면 오프스크린으로 하거나 ENV_BLOCKED로 남긴다.
- Docker 기반 Linux 빌드·실행 환경은 기존 방식대로 쓴다. 기존 이미지·컨테이너는 바꾸지 않는다.
- V4의 시험 전용 하네스 코드는 TestHost에만 둔다. 플러그인(`Source/DTCctv`)에 기존 플러그인 의존을 넣지 않는다.

## 문서

- `docs/FFMPEG_MEDIA_COEXISTENCE.md`: 수정 내용, 원인 근거(엔진 소스·RSP), V1~V6 결과.
- `docs/INTEGRATION_GUIDE.md` 10절과 README 공존 문단: 결과에 맞게 고친다. 통과하면 "Linux에서 둘 중 하나만 활성화" 안내와 `PlatformDenyList` 예시를 공존 가능 안내로 바꾸고, 나중에 기존 플러그인을 제거하는 순서(참조 제거 → `.uproject` 항목 제거 → 폴더 제거 → 재빌드·점검)를 짧게 적는다.
- `Source/ThirdParty/DTCctvFFmpeg/README.md`: 링크 파일 이름이 바뀌면 반영한다.

## 산출물

- 의미 단위 커밋(`CCTV-01V`). 이 지시서도 커밋한다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01V/RESULT.md`, `result.json`, `progress.md`.

완료 보고: 원인과 고른 방법(근거) / V1~V7 판정과 수치 / 동시 재생 결과와 기존 플러그인 쪽 결함 / 미수행·제한 / 결정 필요.
