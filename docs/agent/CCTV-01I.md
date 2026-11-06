# [Claude → Codex] CCTV-01I 기존 FFMPEGMedia 플러그인과의 설치 공존

사용자 요구: 운영 프로젝트에 기존 FFMPEGMedia 플러그인이 추가되어 있는 상태(활성화돼 있지만 CCTV에는 쓰지 않음)에서도 DTCctv가 문제없이 동작해야 한다. 두 플러그인으로 같은 영상을 동시에 재생하는 것은 범위가 아니다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙을 그대로 적용한다. CCTV-01H 이후 최종 코드에서 진행하고, FFmpeg 빌드를 교체했다면 교체한 빌드로 시험한다.

## Claude가 확인한 사실

- 기존 플러그인 소스: `C:\Users\ho270\AppData\Local\Temp\cctv\c` (운영 소스 동기화 커밋 `ea78883`). 원격은 이름이 바뀐 `UE-DT-CCTV-Plugin-FFMPEG`이다. 운영 프로젝트의 실제 버전과 같은지 먼저 확인한다(`C:\Unreal Projects\m7at10_dt`, `ma0t10_dt`의 Plugins 폴더와 비교, 읽기만 한다).
- Windows: 기존 플러그인은 `avcodec-58.dll`, `avutil-56.dll`, `swscale-5.dll`, `swresample-3.dll`, `avformat-58.dll`, `avfilter-7.dll`, `avdevice-58.dll`, `postproc-55.dll`을 지연 로드한다. DTCctv는 `avcodec-62.dll`, `avutil-60.dll` 등이다. 파일 이름은 겹치지 않는다.
- Linux: 기존 플러그인은 `libavcodec.so`(버전 없는 링크)와 버전 파일을 쓰고, DTCctv는 `libavcodec.so.62` 등 SONAME 파일을 실행 파일 옆에 스테이징한다. 한 프로세스에 두 버전의 libav가 함께 올라가므로, 심볼 해석이 섞이지 않는지 확인이 필요하다(심볼 버전 정보 유무가 핵심).

## 검증 항목

1. **구성:** TestHost에 기존 플러그인을 추가하고 활성화한다(정션이나 복사. 원본은 수정하지 않는다). Media Framework 등 기존 플러그인의 의존 플러그인도 운영 프로젝트와 같게 켠다.
2. **빌드와 패키징:** Editor, Win64, Linux 빌드와 패키징. 스테이징 파일 충돌(같은 경로의 다른 파일), 링크 오류, 경고를 확인한다.
3. **로드 확인:** 실행 시 두 플러그인의 FFmpeg 라이브러리가 각각 어느 파일에서 로드됐는지 확인한다. Windows는 모듈 목록, Linux는 `/proc/<pid>/maps`와 `LD_DEBUG=bindings` 일부로 DTCctv의 `av_*` 호출이 자기 버전에 묶이는지 확인한다. `readelf -V`로 양쪽 `.so`의 심볼 버전 정보를 확인한다.
4. **재생:** 기존 플러그인을 쓰지 않는 상태에서 DTCctv 16채널을 측정한다. 게임 60fps 지연 조건, `package_win_16ch`, 5초 정지, fault, Close/Open 30회, Linux 소프트웨어·NVDEC, Pixel Streaming 공존. 기준은 각 게이트와 같다.
5. **기존 플러그인 기능 확인(선택):** 기존 플러그인으로 파일이나 RTSP 하나를 재생해 보고, DTCctv가 로드된 탓에 기존 플러그인이 깨지지 않는지 확인한다. 동시 사용 품질은 판정하지 않는다.

## 재시험 (2026-11-06 사용자 지정)

1차 시험은 `C:\Users\ho270\AppData\Local\Temp\cctv\c`의 `feature/dt-project-sync`(`ea78883`)로 했고, 이 브랜치의 오타(`ststic`, `stoppped`, `FFFMPEGMeiaPlayer`, `static_cat`, `LogFFMPegMedia` 등) 때문에 컴파일 단계에서 막혔다.

사용자가 지정한 기존 플러그인 저장소는 `https://github.com/YonghoKwon/UE-DT-CCTV-Plugin-FFMPEG`다. 기본 브랜치 `main`의 HEAD는 `819e0042bf3da03de50a4c0a9e9d10801b6632bf`(2025-08-18)이고, Claude가 확인한 결과 위 오타가 없다.

- 사용자 설명: 운영 소스와 동기화하는 과정에서 오타가 들어갔을 수 있다. 따라서 운영에 가장 가까운 것은 `feature/dt-project-sync`의 내용이고, 오타만 고친 상태로 본다.
- 이 저장소를 `C:\Users\ho270\AppData\Local\Temp\cctv\ffm-main`에 새로 clone한다. 기존 `c` 폴더와 그 원격 설정은 바꾸지 않는다(그 폴더의 `origin`은 이름이 바뀌기 전 주소라 지금은 DTCctv 저장소를 가리킨다).
- **1순위:** `feature/dt-project-sync`(`ea78883`)를 시험용 복사본에서 **컴파일 오류를 내는 오타만** 고쳐 시험한다. 고친 줄을 모두 원래 값과 고친 값으로 목록에 남긴다(`typo-fixes.md`). 오타가 아니라 로직 판단이 필요한 오류가 나오면 고치지 말고 멈춘 뒤 2순위로 간다. 원격 저장소에는 아무것도 push하지 않는다.
- **2순위:** 1순위가 막히면 `main`의 `819e004`로 시험한다. Claude가 확인한 결과 `main`에는 위 오타가 없다.
- 1차 시험의 정적 검사 결과(Windows DLL 이름 중복 0, Linux 심볼 버전 정의)는 `main`의 라이브러리로 다시 확인한다.
- 시험이 끝나면 TestHost의 FFMPEGMedia 활성화와 복사본을 다시 빼서, 이후 단계의 빌드에 영향이 없게 한다.
- 결과에 `feature/dt-project-sync` 브랜치가 컴파일되지 않는다는 사실과 오타 목록을 별도 항목으로 남긴다. 사용자는 운영 소스 동기화 중 실수를 걱정했다.

## 범위: 확인만 한다 (사용자 지시, 2026-11-06)

사용자는 잘 되는지 안 되는지만 확인하기를 원한다. 플러그인 코드, FFmpeg 빌드, 스테이징은 고치지 않는다.

- 문제가 나오면 증상, 재현 조건, 원인(확인된 것과 추정을 구분)만 기록한다.
- 해결 방안은 후보만 적는다. 예: Windows DLL 전용 폴더와 전체 경로 로드, Linux 고유 이름 FFmpeg 빌드(`--build-suffix`)와 심볼 버전, 스테이징 경로 변경. 수정은 사용자 결정 뒤에 별도로 한다.
- 위 4번 재생 측정은 지연 조건(게임 60fps), `package_win_16ch`, Linux 소프트웨어 실행, Pixel Streaming 공존으로 줄인다. 공존 여부를 판단하는 데 필요한 만큼만 측정한다.

## 산출물

- 의미 단위 커밋(`CCTV-01I`). 이 지시서도 커밋한다. TestHost의 기존 플러그인 추가는 시험용 설정만 커밋하고, 기존 플러그인 소스와 바이너리는 커밋하지 않는다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01I/RESULT.md`, `result.json`, `progress.md`.

완료 보고: 항목 / 커밋 / 게이트별 판정과 수치 / 미수행·제한 / 결정 필요.
