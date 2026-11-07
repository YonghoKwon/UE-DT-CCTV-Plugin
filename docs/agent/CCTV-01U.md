# [Claude → Codex] CCTV-01U 운영 프로젝트 적용 안내서

사용자 요청: 지금 상태의 DTCctv를 실제 운영 프로젝트에 추가해 쓰는 방법을 안내서로 정리한다. 기존 FFmpeg 기반 플러그인(FFMPEGMedia, [UE-DT-CCTV-Plugin-FFMPEG](https://github.com/YonghoKwon/UE-DT-CCTV-Plugin-FFMPEG))과 함께 설치했을 때의 주의점도 담는다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙을 그대로 적용한다. **문서 작업만 한다.** C++, 빌드 규칙, FFmpeg, 설정 기본값은 바꾸지 않고, 새 빌드·패키징·측정도 하지 않는다.

## 독자와 전제

- 독자: DTCore 기반 운영 프로젝트(예: `ma0t10_dt`, `m7at10_dt`)에 CCTV 화면을 붙이는 개발자. UE 5.3, Windows·Linux 패키징, Pixel Streaming 사용.
- 운영 프로젝트는 읽기만 한다. 수정, 빌드, 커밋하지 않는다. 확인할 것: `.uproject` 플러그인 목록, `Plugins/` 구성(DTCore·DTDevice가 Git 서브모듈인지), `.gitattributes`의 LFS 규칙, DTCore 로그 API 위치.
- 이 PC의 두 운영 프로젝트 작업 폴더에는 현재 FFMPEGMedia가 없다. 안내서에는 "설치되어 있는 경우"로 쓴다.

## 산출물

1. `docs/INTEGRATION_GUIDE.md`(한국어): 운영 적용 안내서.
2. README 맨 위 소개 문단 바로 아래에 안내서로 가는 짧은 링크 한 줄. 기존 README의 "프로젝트에 적용" 절은 안내서와 겹치므로 요약 3~5줄과 안내서 링크로 줄인다. 시험 기록 절은 건드리지 않는다.
3. 의미 단위 커밋(`CCTV-01U`). 이 지시서도 커밋한다. push, PR, merge는 금지다.
4. `Saved/Reports/CCTV-01U/RESULT.md`: 확인한 근거(파일·줄), 검증 못 한 내용.

## 안내서 목차와 요구사항

모든 API, 설정 이름, 기본값, 명령줄 옵션, 파일 경로는 **현재 소스(`85f4e49`)에서 확인한 것만** 쓴다. 확인하지 못한 내용은 쓰지 않거나 "미검증"으로 표시한다. 시험 기록을 다시 늘어놓지 말고, 수치는 꼭 필요한 것만 README 해당 절로 링크한다.

1. **한눈에 보기**: 무엇을 하는 플러그인인지, 지원 범위(Windows·Linux, UE 5.3, 코덱), 지원하지 않는 것(오디오, 탐색 등).
2. **설치**
   - Git LFS 준비, `git lfs pull`, LFS 포인터 상태로 빌드하면 안 되는 이유.
   - 운영 프로젝트에 넣는 방법: 복사 방식(가져올 폴더와 가져오지 말 폴더)을 기본으로 쓴다. 서브모듈 방식은 이 저장소 루트에 TestHost 등이 함께 있어 문제가 되는지 실제 구조로 판단해 권장 여부를 적는다.
   - 운영 저장소에 FFmpeg DLL·`.so`를 커밋할 때 LFS 규칙이 필요한지 운영 프로젝트의 `.gitattributes`를 보고 적는다. Linux `.so` 링커 스크립트와 버전 파일을 바꾸면 안 된다는 주의도 넣는다.
   - `.uproject` 항목, 호출 모듈 `.Build.cs` 의존성.
3. **DTCore 로그 연동**: 감지 방식(빌드 시 무엇을 보고 `DTCCTV_WITH_DTCORE`를 켜는지), 에디터와 패키지에서 로그가 어디로 가는지, DTCore가 없을 때의 동작, 연동이 켜졌는지 확인하는 방법.
4. **화면 만들기**: UMG `DTCctvView` 사용 순서, 상태 표시(재연결 등)를 운영 위젯이 맡는다는 점, 화면을 닫을 때 `Stop`/`CloseStream`. C++ 예시는 README와 같은 내용을 짧게.
5. **카메라 주소와 보안**: 계정이 든 URL을 에셋·소스·로그에 남기지 않는 방법, `DTCctvMaskUrlCredentials`, 메인·서브 스트림 구성 권장.
6. **프로젝트 설정 권장값**: `UDTCctvSettings` 항목 표(이름, 기본값, 언제 바꾸는지). `bIgnoreWindowsTimerThrottling`과 `-DTCctvIgnoreTimerThrottling=1/0` 포함. `DefaultGame.ini` 예시.
7. **채널 수 계획**: README 코덱별 표 요약과 링크, 서브 스트림 활용, 운영 서버에서 다시 확인해야 한다는 점.
8. **Pixel Streaming 서버 실행 권장**: 오프스크린 실행, 창 포그라운드, 타이머 옵션 중 무엇을 쓰는지. 실제 동시 부하는 미검증임을 밝힌다.
9. **배포 전 점검**: TestHost 연결 점검 도구로 운영 카메라 주소를 확인하는 방법(실행 명령, 주소 입력 방법, PASS·WARN·FAIL 읽는 법), 스트림 정보 로그로 코덱 확인, Windows·Linux 패키지에 FFmpeg 파일이 제대로 들어갔는지 확인하는 방법, Linux 서버의 `linux_server_check.sh`.
10. **기존 FFMPEGMedia 플러그인과 함께 설치할 때**: 아래 내용을 표로 정리한다.
    - Windows: 기존 플러그인을 켜 두고 쓰지 않는 조건에서 DTCctv 16채널 정상(01I). 기존 `avdevice-58.dll` 로드 오류 126은 DTCctv 없이도 나는 기존 플러그인 쪽 문제.
    - Linux: 두 플러그인을 함께 켜면 패키징 링크가 실패한다(같은 `-lavcodec` 등 링크 이름 충돌, 01I·R15). 해결 전까지는 Linux 대상에서 둘 중 하나만 켜야 한다. `.uproject` 플러그인 항목의 플랫폼 제한(UE 5.3에서 실제 쓰는 필드 이름을 엔진 소스로 확인)으로 Linux에서만 기존 플러그인을 끄는 방법은 **미검증**으로 적고, 그 경우 운영 코드가 Linux에서 기존 플러그인 모듈을 참조하지 않아야 한다는 조건을 적는다.
    - 두 플러그인으로 동시에 영상을 재생하는 시험은 하지 않았다.
    - `feature/dt-project-sync`(ea78883)는 오타 수정 후에도 빌드되지 않는다. 시험은 main(819e004)으로 했다.
11. **문제 해결**: 상태별 의미(Connecting, Playing, Reconnecting, Idle), 자주 보는 원인(403·401 인증, UDP 차단 시 TCP 사용, 코덱 미지원, 채널 과다, 백그라운드 창 FPS 저하), 로그에서 볼 것.
12. **운영에서 아직 확인해야 하는 것**: Linux Vulkan 화면 표시, Pixel Streaming 동시 부하, 실제 카메라·원격지 TCP, Close/Open 장시간 자원 관찰.
13. **적용 체크리스트**: 위 내용을 순서대로 체크 항목 10~15개로.

## 문체

- 짧은 문장, 단계는 번호 목록, 비교는 표. 명령과 경로는 코드 서식.
- 측정 경위, 커밋 해시 나열, 시도 기록은 넣지 않는다. 근거가 필요하면 README·docs 해당 절로 링크한다.
- 비밀값, 실제 카메라 주소, 사내 주소를 넣지 않는다. 예시 주소는 `rtsp://<카메라 주소>/...` 형식.

완료 보고: 안내서 목차 / 소스로 확인한 항목과 근거 / 미검증으로 표시한 항목 / README 변경 요약.
