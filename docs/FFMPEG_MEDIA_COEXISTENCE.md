# FFMPEGMedia 설치·실행 공존 검증

## CCTV-01Y 현재 결론 (2026-11-07)

**main `819e004`와 함께 설치·동시 재생 가능.** Windows·Linux에서 기존 플레이어4+DTCctv12를 120초 이상 실제 재생했고 크래시·교착·함수 섞임0으로 통과했다. 기존 플러그인의 소스·빌드 규칙·바이너리·descriptor와 양쪽 FFmpeg 바이너리는 수정하지 않았다. 기존 원본1022개 파일, DTCctv 동봉 런타임10개 해시를 확인했다. 운영 프로젝트는 읽기만 했고 적용하지 않았다.

**2026-11-07 사용자 결정: 현재 재연결 동작을 수용하고 장애 복구 기준을 6.5초로 고친다.** 코드·기본값은 바꾸지 않는다. 6회 모두 6.5초 이내라 이 기준으로 Y6는 PASS다. 아래 5초 판정 기록은 당시 원문으로 보존한다. 후속 현재/직전 각3회 분포를 아래에 기록했다. Close/Open GPU는 재측정하지 않았다. 추가 지시의 Claude 검토에 따라 첫끝1100.6328125MiB·최저값과4KiB 차이를 측정 잡음으로 보아 증가로 판정하지 않는다(PASS). 원시 OLS+0.0000473485MiB/회와1차FAIL 자료는 보존했다. 공존 성공과 복구 시간 실패를 구분한다.

### Y6 후속 복구 시간 비교 (2026-11-07)

| 버전 | 회차 | 전체복구(초) | 채널 최소/중앙/p95/최대(초) | 재연결 | 5초 판정 |
|---|---:|---:|---|---:|---|
| 현재 | 1 | 5.646343 | 3.646343/4.646343/5.646343/5.646343 | 96 | FAIL |
| 직전62d9b58 | 1 | 6.032307 | 4.031307/5.031807/6.032307/6.032307 | 96 | FAIL |
| 현재 | 2 | 6.004533 | 1.985533/4.002533/6.004533/6.004533 | 90 | FAIL |
| 직전62d9b58 | 2 | 5.982688 | 1.981688/3.981688/5.982688/5.982688 | 94 | FAIL |
| 현재 | 3 | 3.985005 | 1.984005/2.984505/3.985005/3.985005 | 84 | PASS |
| 직전62d9b58 | 3 | 5.887719 | 1.886719/3.886719/5.887719/5.887719 | 93 | FAIL |

두 코드의 전체 복구 시간 분포가 겹친다. 지시서 판정에 따라 이번 함수 표 변경과 무관한 재연결/GOP 편차로 기록한다. 표본3회씩의 관측 범위를 넘어 인과를 확정하지 않으며 5초 초과 결과는 FAIL·사용자 결정 사항으로 유지한다.

동일 H60 16채널·송출8초 중단·오프스크린·타이머 기본 꺼짐, 현재/직전 교대 직렬 측정이다. 기존 플러그인은 활성화하지 않았다. 약1초 CSV의 준비로그 기준 복구 시간을 사용했다. 두 격리 복사본의 새 Editor/Windows 패키지로 시험했고 C++·FFmpeg·기본값은 바꾸지 않았다. 이전 입력 파일은 정리돼 동일 인자로 재생성했으며6회는 같은 새 파일SHA를 공유한다. 채널별16개 시간·재연결 분포와 정리 근거는 Saved/Reports/CCTV-01Y/Y6-recovery-followup 및 measurements/Y6-followup-*에 있다. 복구5초 초과의 수용·후속 검증 범위는 사용자 결정 사항이다.

### 호출 분리 방법

수정 전 Windows COFF·Linux ELF 오브젝트의 FFmpeg 미해결 심볼은 플랫폼별52개로 같았다. 이 합집합 전체를 단일 함수 표로 옮겼다. 헤더 인라인·매크로가 남긴 호출도 오브젝트 검사 대상으로 삼았고, 수정 후 FFmpeg 미해결 심볼은0이었다. HW 장치·프레임·사전·로그·패킷·코덱·네트워크·스케일러 호출이 포함된다. 추가 브리지 바이너리는 만들지 않았다.

DTCctv 빌드의 `.lib`와 `libdtcctv_*.so` 링크 입력을 제거했다. Windows는 스테이징 전체 경로 `LoadLibraryExW`와 `GetProcAddress`, Linux는 전체 경로 `dlopen(RTLD_NOW | RTLD_LOCAL)`과 `dlsym`으로 avutil→swresample→swscale→avcodec→avformat 순서로 연다. Editor는 DTCctv 모듈 Binaries, 패키지는 실행 파일 옆의 동일 이름·해시 파일을 사용한다. 파일·함수 하나라도 누락되면 표를 게시하지 않고 부분 핸들을 역순 정리한다. 스트림은 세션 생성 없이 Idle·LastError로 끝난다. 정상 종료에서는 세션·렌더·HW 소유자0을 확인한 후 역순 해제했다. 남은 소유자가 있으면 프로세스 수명까지 유지하는 방어 경로가 있다.

Windows 실제 IAT의 구 호출 주소는 구58계열 DLL, DTCctv 함수 표52개 주소는 신 계열 DLL 범위에 있었다. Linux LD_DEBUG와 maps에서 구 호출은 구 계열, 신 라이브러리 내부 참조도 신 계열이었다. 섞임0이므로 `RTLD_DEEPBIND`는 사용하지 않았다.

### 01Y 시험 결과

| 게이트 | 판정 | 근거 |
|---|---|---|
| Y1 | PASS | 단독·공존 Win PE/Linux ELF와 Editor DLL의 신 계열 import·DT_NEEDED0, DTCctv 오브젝트 미해결 FFmpeg0 |
| Y2 | PASS | 전체30개 단위 성공(일반29·경고동반1), 실패·미수행0. 파일·함수 누락 모의 포함 |
| Y3 | PASS | 복사 패키지 avcodec62 제거 후 크래시0·종료0·정확한 파일 오류. 연결 점검 자체는 예상한 FAIL |
| Y4 | PASS | 단독16채널: Win1080p60 decode/display59.9874/59.8671fps·p95 36ms, Linux30fps SW29.9882·자동29.9874fps. V6 오차2% 이내 |
| Y5 | PASS | C1/C3/C6/C7/C8/H60×Windows CUDA/D3D11VA·Linux SW/CUDA 24조합, 각4채널 유효60초 이상. MJPEG는 지원되는 SW 폴백, Windows12조합 PNG 직접 검수 |
| Y6 | PASS(6.5초 기준, 사용자 결정) | 후속 현재3회 5.646343, 6.004533, 3.985005초, 직전3회 6.032307, 5.982688, 5.887719초. 5초 기준 유지·분포 겹침·일관된 현재 지연 없음. Close/Open GPU는 추가 지시의4KiB 잡음 해석으로 PASS·재측정 없음. 서브 전환 PASS |
| Y7 | PASS | 공존·기존 미사용16채널: Win59.9790/59.8823fps·p95 36ms, Linux SW30.0224·자동30.0105fps |
| Y8 | PASS (Linux TCP) | 기존만4채널: Win유효124.936초·채널당3748~3749샘플, LinuxTCP124.937초·각3748샘플. 시각 진행·Playing·오류0·종료0. 초기LinuxUDP 프레임0 FAIL 보존 |
| Y9 | PASS | 아래 동시 재생 표. 양 플랫폼120초 이상·크래시/교착0 |
| Y10 | PASS | 실제 함수 주소·IAT·LD_DEBUG/maps에서 신·구 호출 섞임0·신 라이브러리 내부 결합 정상 |
| Y11 | PASS | 공존 Editor 빌드·실제 GUI PIE 4+1 재생49.4초, 기존1456샘플·시각1455회 진행, 정상 Stop/종료0 |
| Y12 | PASS | descriptor 원문 복원·시험 복사본 제외, 사전 계획24경로18282파일22,421,291,846바이트 삭제. 소유 프로세스/CID잔여0·기존Docker불변 |

| 동시 재생 | DTCctv12 (HW4+SW8) | 기존4 | 공통 |
|---|---|---|---|
| Windows | 유효124.2초, decode29.9980/display29.9987fps, p95 20ms, 프레임 대체0, PNG16타일 정상 | 각3726샘플·시각 진행 | 전채널Playing, DTO손실/재연결/디코딩오류0, 종료0 |
| Linux TCP | 유효123.9초, decode30fps, 최저채널29.9976fps | 각3717샘플·시각 진행 | 전채널Playing, DTO손실/재연결/디코딩오류0, 종료0 |

### 적용과 남은 제한

01V의 ABI 충돌 회피용 `PlatformDenyList`는 **필요 없다**. Linux의 기존 플레이어는 이 컨테이너 환경의 기본UDP에서 프레임0·타임아웃이었고, 기존 설정 클래스의 RTSP TCP를 **시험 프로세스 명령줄에만** 적용한 재시험에서 통과했다. nullrhi에서도 실제 비디오 샘플 진행을 확인했으므로 Linux 항목을 ENV_BLOCKED로 처리하지 않았다. 이 전송 설정 선택은 기존 소스·descriptor·설정 파일·기본값 변경이 아니다. 초기 실패는 gzip 원문과 분석으로 보존한다.

구 `avdevice-58.dll` 오류126은 종전 단독 대조에서도 확인된 별도 제한으로 고치지 않았다. Linux 화면 렌더링·Pixel Streaming 인코딩 동시 부하·실제 카메라·운영 프로젝트·장시간 수명 수용은 이번 범위 밖이다. 함수·파일 누락 모의와 실제 파일 누락은 시험했지만 실제 변조 DLL의 함수 누락, 소유자 잔존 상태의 장시간 종료는 별도로 실행하지 않았다. Y6는 사용자 결정으로 6.5초 기준을 적용해 종결했다.

나중에 기존 플러그인을 제거할 때는 운영 코드·미디어 에셋 참조 제거→`.uproject` 활성 항목 제거→플러그인 폴더 제거→양 플랫폼 재빌드·패키징·점검 순서로 진행한다. 이번 작업에서는 운영 프로젝트를 바꾸지 않았다.

상세 자료: `Saved/Reports/CCTV-01Y/RESULT.md`, `result.json`, `function-inventory.json`, `Y1-static-analysis.json`, `Y5-codec-analysis.json`, `Y10-binding-analysis.json`, `Y11-pie-analysis.json`, `measurements/`, `cleanup-plan.md`, `scope-before-cleanup.json`, `cleanup-audit.json`. 아래 01V·01I·R15는 당시 기록으로 보존한다.

## CCTV-01V 당시 결론 (01Y 이전)

**전체 FAIL.** Windows·Linux의 두 플러그인 활성 빌드·패키징과 DTCctv 단독 재생은 확인했지만, 실제 4+12 동시 재생은 두 플랫폼 모두 충돌했다. 기존 FFMPEGMedia main `819e004`의 소스·빌드 규칙·바이너리는 수정하지 않았다. 아래 이전 01I·R15 기록은 당시 결과로 보존한다.

### 원인과 링크 수정

UE 5.3의 `Engine/Source/Programs/UnrealBuildTool/Platform/Linux/LinuxToolChain.cs` 1104–1120행은 시스템 라이브러리와 파일 라이브러리를 합쳐 경로를 모은다. 1199–1204행은 이를 `-L`로 전달하고, 1218–1266행은 공유 라이브러리 파일명에서 마지막 확장자와 `lib`를 제거해 `-l이름`으로 바꾼다. 따라서 버전 `.so.62` 전체 경로도 `-lavcodec.so`가 되어 직접 지정 후보로 사용할 수 없다. `PublicSystemLibraries`의 `:libavcodec.so.62`는 소스상 정확한 이름 전달이 가능하지만 이번에는 파일 의존성이 남는 고유 스크립트 방식을 골랐다.

DTCctv의 무버전 스크립트 5개를 `libdtcctv_avcodec.so` 등으로 바꿨다. 내용은 기존과 같은 `INPUT(libavcodec.so.62)` 등이며 실제 링크는 `-ldtcctv_avcodec` 등이 된다. 일반 `libavcodec.so` 등이 DTCctv 폴더에 없어 기존 플러그인의 `-lavcodec` 검색을 가로채지 않는다. FFmpeg 바이너리·SONAME·런타임 스테이징 이름은 유지했다. 실제 생성물은 오브젝트 목록 RSP와 라이브러리 인자를 붙이는 `Link-*.bat`로 나뉘므로 둘을 보존했다.

**검색 이름 분리만으로 함수 import는 분리되지 않는다.** Linux monolithic ELF의 공통 API는 신 버전 import로 합쳐졌다. 기존 플러그인 전용 호출인 `avformat_license()`도 실제 `LD_DEBUG=bindings`에서 `LIBAVFORMAT_62`로 결합됐다. maps에는 양 계열 파일이 있고 각 DSO의 SONAME·심볼 버전도 다르지만, 이것만으로 호출 섞임을 막지 못했다.

Windows PE에서도 `avformat_open_input`·`avformat_find_stream_info`는 `avformat-62.dll`, 구 전용 `av_format_inject_global_side_data`는 `avformat-58.dll` delay import였다. 기존 `ReadContext()`가 같은 컨텍스트에 이 호출들을 사용하며, Windows는 구 avformat/ReadContext 스택, Linux는 `libavformat.so.58!av_format_inject_global_side_data`에서 충돌했다. 자료를 종합하면 신·구 컨텍스트 ABI 혼합이 원인으로 판단된다. 기존 플러그인 단독 재생에서 이 충돌을 재현한 것은 아니므로 자체 단독 결함으로 단정하지 않는다.

### V1~V7 결과

| 항목 | 판정 | 범위·수치 |
|---|---|---|
| V1 Linux 공존 빌드·패키징 | PASS | 종료0, 고유 링크5개와 구 링크 요청 분리, ELF에 신5개와 구 avformat58·avutil56·swresample3 포함. 모든 구5개가 직접 NEEDED여야 한다는 추가 조건은 요구하지 않으며 런타임 분리는 V3로 별도 판정 |
| V2 Linux DTCctv16, 기존 미사용 | PASS | H.264 High 1080p30. SW 평균 29.978fps, 자동 CUDA4/SW12 29.999fps. 각 유효 89.3초, 채널별97%·전16 Playing·손실/재연결/오류0. nullrhi 디코딩 범위 |
| V3 런타임 결합 | FAIL | maps에 구·신 계열, DTCctv 공통 호출은 신 계열. 그러나 기존 호출도 신 버전에 결합돼 섞임0 실패. Windows PE에서도 같은 import 혼합 확인 |
| V4 두 플레이어 동시 재생 | FAIL | 기존4+DTCctv12, 135초 요청. Windows 네이티브3, Linux SIGSEGV(-11)로 열기 직후 충돌. 120초 미완료·유효 FPS 없음. nullrhi 환경 차단으로 바꾸지 않음 |
| V5 Windows 공존, 기존 미사용 | PASS | 새 패키지 종료0. H.264 1080p60·16채널, 오프스크린1920×1080·타이머 기본꺼짐, 유효 89.3초. decode 59.973/display 59.908fps, p95 35ms·대체 0.0980%, 손실/재연결/오류0·PNG 정상 |
| V6 DTCctv 단독 회귀 | PASS | Editor·Win64/Linux 새 빌드·패키징0. 단위26+경고성공1, 실패/미수행0. 각95초 재생: Win60fps 59.999/59.929, Linux30fps SW 29.964·자동 29.976. 유효84초 이상, 전16 Playing·손실/재연결/오류0. SONAME5개 원본 해시 동일·구계열 NEEDED 없음 |
| V7 정리 | PASS | TestHost descriptor 원문 복원, 기존 시험 복사본은 빌드 검색 경로에서 제거. 원본·운영 파일·기존 Docker 보존. 계획 후 자체 대용량 산출물 정리, 요약·원시 증거·대표 PNG 보존 |

Windows 측정 직전 입력 데스크톱은 OpenInputDesktop으로 `Default`임을 확인했고 실행 방식은 오프스크린이었다. Linux의 표시·색·Vulkan·Pixel Streaming 실제 송출 동시 부하는 검증하지 않았다. 실제 카메라 대신 합성 반복 파일을 복사 송출했으며 측정은 모두 직렬로 했다.

구 `avdevice-58.dll` 오류126은 이번 Windows 로그에도 남고, R15에서 단독 재현된 기존 제공본·의존 환경 문제다. 이 문제와 이번 공존 import 혼합 충돌을 구분한다. 기능을 제거하거나 기존 플러그인을 수정해 통과시키지 않았다.

### 남은 결정과 적용 안내

지시서에 따라 섞임 확인 후 임의 우회를 하지 않았다. DTCctv의 FFmpeg 호출만 고유 export의 별도 공유 브리지에 옮기는 구조가 다음 검토 후보이며 **미구현·미검증**이다. 새 DLL/SO의 빌드·스테이징 범위와 V1의 의존성 그래프 확인 대상을 먼저 정해야 한다. 동시 재생120초·호출 섞임0과 기존 회귀 기준은 유지한다.

현재 결과로 안전한 동시 재생을 승인하지 않는다. Linux 단일 플러그인 안내와 플랫폼 제외 예시는 [운영 안내서 10절](INTEGRATION_GUIDE.md#10-기존-ffmpegmedia-플러그인과-함께-설치할-때)에 유지했다. 나중에 기존 플러그인을 제거할 때는 참조 제거 → `.uproject` 항목 제거 → 폴더 제거 → 양 플랫폼 재빌드·점검 순서로 진행한다. 운영 프로젝트에는 실제 적용하지 않았다.

상세 자료는 `Saved/Reports/CCTV-01V/RESULT.md`, `V1-linux-link.bat`, `V1-linux-link.rsp`, `V1-elf-dynamic.txt`, `V3-analysis.json`, `Windows-import-analysis.json`, `measurements/`, `DECISION_REQUIRED.md`에 있다. 원본 clone·FFmpeg·설정 기본값·제품 Source/DTCctv는 보존했다. 커밋은 로컬이며 push·PR·merge는 하지 않았다.

---

## 이전 CCTV-01I·R15 보존 기록

2026-11-06 CCTV-01I 재시험 결과다. DTCctv 코드는 `f6192e9`이며 FFmpeg 빌드·바이너리·스테이징 규칙은 변경하지 않았다. 두 플러그인으로 영상을 동시에 재생하는 시험은 아니다.

## 시험 소스와 전환 사유

사용자가 지정한 [UE-DT-CCTV-Plugin-FFMPEG](https://github.com/YonghoKwon/UE-DT-CCTV-Plugin-FFMPEG)를 임시 폴더 `ffm-main`에 새로 clone했다. 기존 `c` 원본과 그 원격 설정은 보존했다.

1. `feature/dt-project-sync`의 `ea7888327f89022e6435b07837b15930bcc02dfd`를 독립 시험 복사본에 복사했다. 아래 철자 오타 5줄만 수정했지만 Editor 빌드는 `FFMPEGMediaTracks.cpp:2201`의 `LIVE_MAX_AUDIO_PACKETS` 미선언(C2065, 종료 6)으로 실패했다. 이 상수는 main에도 대응 선언이 없고 값을 정해야 하므로 수정하지 않았다.
2. 지시서의 예비안에 따라 `main`의 `819e0042bf3da03de50a4c0a9e9d10801b6632bf`로 전환했다. main 소스는 수정하지 않았다.

아래 파일 경로의 기준은 `Source/FFMPEGMedia/Private/Player/`다. 원래 줄과 고친 줄의 전체 내용은 `Saved/Reports/CCTV-01I/typo-fixes.md`에 보존했다.

| 파일·줄 | 원래 토큰 | 고친 토큰 |
|---|---|---|
| FFMPEGMediaPlayer.h:73 | `ststic` | `static` |
| FFMPEGMediaPlayer.cpp:317 | `stoppped` | `stopped` |
| FFMPEGMediaPlayer.cpp:326 | `LogFFMPegMedia` | `LogFFMPEGMedia` |
| FFMPEGMediaTracks.cpp:2254 | `FFFMPEGMeiaPlayer` | `FFFMPEGMediaPlayer` |
| FFMPEGMediaTracks.cpp:2255 | `static_cat` | `static_cast` |

## main 재시험 결과

| 게이트 | 판정 | 근거 |
|---|---|---|
| Editor / Windows 패키징 | PASS | 각각 종료 0, 새 공존 패키지 사용 |
| Windows 16채널·게임 60fps 지연 | PASS | 일반 창 포그라운드 유지, 1080p60, 90.3초. 디코딩 59.9924fps / 표시 59.9398fps / 채널 최악 p95 36ms / 프레임 대체 0.1004%. 손실·재연결·오류 0, 전16 Playing, 1920×1080 PNG 직접 검수 |
| Windows DLL 경로 | 일부 실패 | 신 FFmpeg 5개·구 7개가 각자의 패키지 경로에서 로드됐으며 원본 해시와 일치. 구 `avdevice-58.dll`은 오류 126으로 실패. 로그는 `OpenAL32.dll`, `AVICAP32.dll`을 Missing import로 표시. System32의 AVICAP32.dll은 존재하므로 해당 파일 자체가 없다고 판단하지 않음 |
| Windows Pixel Streaming | PASS | 세 플러그인 로드와 전16 Playing 확인. 실제 시그널링·영상 송출·인코딩 동시 부하는 미검증 |
| Linux 패키징 | FAIL | 구 API `av_format_inject_global_side_data`, `av_get_channel_layout_nb_channels`, `av_get_default_channel_layout`, `swr_alloc_set_opts` 미해결 심볼, 종료 6 |
| Linux 소프트웨어·maps·bindings·Pixel Streaming | NOT_RUN | 공존 실행 파일 링크 실패. 기존 단독 패키지를 대신 쓰지 않음 |
| 정적 라이브러리 검사 | PASS | main의 Windows DLL 이름 중복 0, Linux 구8·신5 라이브러리 SONAME과 심볼 버전 정의 확인. 런타임 성공을 뜻하지 않음 |

Linux 생성 링크 명령은 두 플러그인의 라이브러리를 같은 `-lavcodec` 등의 이름으로 요청하고, 검색 경로에는 DTCctv가 먼저 온다. 실패한 네 함수는 구 라이브러리에만 export된다. 같은 링크 이름과 검색 경로 순서에 따른 선택 충돌이 원인으로 판단된다. 실제 링크 추적과 런타임 심볼 결합은 실행하지 못했으므로 그 범위는 미확정이다. 빌드 규칙·이름·FFmpeg를 변경하는 해결은 이번 작업에서 하지 않았다.

따라서 **main 플러그인을 활성화하고 사용하지 않는 Windows 조건에서 DTCctv 재생은 확인했지만, Linux 공존 빌드는 실패했다.** 기존 플러그인 자체의 완전한 로드·재생 정상 여부는 수용하지 않는다. 운영 프로젝트의 실제 설치본 동일성은 이전 시험에서 확인하지 못했고, 이번에는 사용자가 지정한 커밋을 시험했다.

실행기 경로 오류 1회와 빈 CSV 표본 접근 오류 1회는 자료를 보존한 뒤 수정했다. 두 실행의 성능값은 채택하지 않았다. 세 번째 실행만 위 판정에 사용했다. TestHost의 FFMPEGMedia 활성화는 원문으로 복원했고 두 시험 복사본은 Saved 아래로 옮겼다. 기존 1차 결과는 `Saved/Reports/CCTV-01I/first-trial-preserved/`, 재시험 자료는 `Saved/Reports/CCTV-01I/retest/`에 있다.

## R15 단독 Linux 빌드

2026-11-06 보충 확인에서 DTCctv를 끄고 FFMPEGMedia `main(819e004)`만 활성화한 Linux 링크는 **PASS(종료 0)**였다. 따라서 이 PC에서 기존 플러그인이 단독으로도 Linux 빌드가 불가능한 것은 아니다. 앞선 공존 링크 실패와 단독 성공을 구분해야 한다.

기존 TestHost 하네스는 DTCctv에 직접 의존한다. 코드 수정을 피하기 위해 원래 `TestHost/Source`를 잠시 빌드 경로 밖에 보존하고 프로젝트의 하네스 모듈 목록을 임시로 제외했다. UE의 `BuildTarget` 명령이 `Intermediate/Source`에 기본 하네스와 타깃을 자동 생성하게 하여 실제 단독 링크 1회를 수행했다. PixelStreaming도 이 대조 구성에서는 껐다. 원래 하네스·타깃·플러그인·FFmpeg 코드는 수정하지 않았다.

생성 링크 명령에 FFMPEGMedia 오브젝트가 포함되고 DTCctv 오브젝트와 FFmpeg 검색 경로가 없는 것을 확인했다. 생성 ELF의 `DT_NEEDED`는 `libavcodec.so.58`, `libavformat.so.58`, `libavutil.so.56`, `libswresample.so.3`, `libswscale.so.5`이며 DTCctv의 신 버전은 없다. 앞선 공존 명령에서 확인한 동일 `-l` 이름과 검색 순서의 충돌 해석을 뒷받침한다. 다만 두 구성은 원래 하네스와 PixelStreaming 활성화도 다르며, 링커 선택 추적이나 Linux 실행은 하지 않았다.

기존 플러그인의 Linux `.so` 24개는 **모두 실제 ELF 바이너리**다. Git mode도 전부 `100755` 일반 파일이고, 무버전·주버전·전체버전 파일은 같은 계열별 동일 blob이다. `core.symlinks=false`이지만 이 저장소의 해당 파일은 Git 심볼릭 링크(`120000`)가 아니므로 Windows에서 링크가 텍스트로 풀린 문제가 아니다.

Windows Editor-Cmd NullRHI에서 DTCctv 모듈·신 FFmpeg DLL이 전혀 없는 상태로 구 DLL 7개의 경로·해시를 확인했다. 이전 공존 측정과 동일한 main DLL이며, **`avdevice-58.dll` 오류 126도 단독 상태에서 재현**됐다. 이 로드 실패는 DTCctv 공존을 필수 조건으로 하지 않으므로 기존 제공본·의존 환경 문제로 구분한다. Windows 확인은 모듈 로드만이며 기존 플러그인의 재생 품질은 시험하지 않았다.

초기 기본 `UnrealGame` 명령은 종료 0이었지만 메타데이터만 갱신해 결과를 채택하지 않았다. 실제 단독 링크 성공은 UE 자동 생성 타깃의 결과만 사용했다. Windows 관측 준비의 BOM 오류와 첫 실행의 로그 공유 읽기 오류도 기록하고, 최종 관측만 채택했다.

시험 뒤 원래 Source·프로젝트 설정을 복원하고 FFMPEGMedia 복사본·자동 생성 소스·단독 실행 파일과 receipt를 빌드 경로 밖으로 옮겼다. 상세 자료는 `Saved/Reports/CCTV-01I/R15/`, 추가 보고는 기존 `RESULT.md`·`result.json`의 R15 항목에 있다. 앞선 1차 및 공존 재시험 결과는 보존했다.
