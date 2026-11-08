# [Claude → Codex] CCTV-01P 보기 모드와 테스트용 Windows·Linux 패키지

사용자 요청(2026-11-08): CCTV 테스트 전용 Windows·Linux 패키지를 만든다. 실행하면 창에서 RTSP 주소를 N개 넣고 **보기** 버튼을 눌러 잘 보이는지 확인할 수 있어야 한다. 패키지에는 Pixel Streaming이 들어 있어 그것도 시험할 수 있어야 한다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙을 그대로 적용한다.

추가 규칙:
- 다른 세션이 `C:\Project\digital-twin-simulator`에서 작업 중이다. 그 폴더와 그 세션의 프로세스는 건드리지 않는다. 빌드 전 다른 UE 작업이 있으면 기다린다. 프로세스는 내가 띄운 PID로만 종료한다.
- `C:\Unreal Projects\PixelStreamingInfrastructure`는 사용자 저장소다. **읽기·실행만** 하고 파일을 바꾸지 않는다(설정은 명령줄 인자나 저장소 밖 임시 설정 파일로 준다). `npm install` 등 새 다운로드는 하지 않는다. 이미 있는 `node_modules`로 실행이 안 되면 멈추고 보고한다.
- 기존 FFMPEGMedia 플러그인과 운영 프로젝트는 건드리지 않는다. 플러그인 `Source/DTCctv`의 동작은 바꾸지 않는다(TestHost만 수정).

## 1. 보기 모드 (TestHost 점검 화면)

- 기존 **시작**(시간 제한 점검·보고서) 옆에 **보기** 버튼을 추가한다. 보기는 입력한 주소 전부를 시간 제한 없이 계속 재생한다. 판정·보고서는 만들지 않는다.
- 보기 중에는 기존 격자 표시(영상, 상태, 코덱, 해상도, FPS, HW/SW, 재연결, 오류)와 1열/2열/4열, 창 크기 조절을 그대로 쓴다.
- **중지**로 보기를 끝낸다. 보기 중 주소 추가·삭제는 막거나, 바꾼 뒤 다시 보기를 눌러야 반영되게 한다. 둘 중 단순한 쪽을 고르고 화면에 알 수 있게 한다.
- 보기와 시작은 동시에 실행되지 않는다. 보기 중 시작을 누르면 막거나 보기를 끝내고 시작한다(단순한 쪽).
- 계정 가림, 주소 저장(계정 제외) 등 기존 보안 동작은 그대로다.
- 명령줄로도 열 수 있게 한다: `-CctvView` + 기존 `-CctvCheckList=<json>`이면 그 목록으로 바로 보기를 시작한다(Pixel Streaming 원격 시험과 Linux 시험에 쓴다).
- 단위 테스트: 보기 상태 전환(보기→중지, 보기 중 시작, 빈 목록에서 보기 비활성)을 추가한다.

## 2. Pixel Streaming

- TestHost에 Pixel Streaming 플러그인은 이미 켜져 있다. 기본 실행(인자 없음)은 지금처럼 로컬 창만 띄운다.
- `-PixelStreamingURL=ws://<서버>:8888`로 실행하면 스트리밍한다. 브라우저에서 화면이 보이고, **브라우저에서 마우스·키보드로 주소 입력, 추가, 보기, 중지를 조작할 수 있어야 한다.** 필요한 입력 설정(예: `-AllowPixelStreamingCommands` 여부, 키보드 입력 처리, 마우스 커서 표시)은 UE 5.3 Pixel Streaming 소스·문서로 확인해 적용하고 근거를 남긴다.
- 인코더는 운영과 같은 H.264(`-PixelStreamingEncoderCodec=H264`)를 기본으로 한다.
- 시그널링 서버는 `PixelStreamingInfrastructure`의 `SignallingWebServer`를 그대로 실행한다. 포트가 다른 프로그램과 겹치면 다른 포트를 인자로 준다.

## 3. 패키지

- 현재 HEAD 코드로 Win64·Linux **Development** 패키지를 새로 만든다(로그 확인용).
- 출력 위치: `C:\CCTV-TestPackages\2026-11-08\`(저장소 밖, 정리 대상 아님). 그 안에 `Windows\`, `Linux\`, 그리고 전달용 `DTCctvTestHost-Win64.zip`, `DTCctvTestHost-Linux.tar.gz`(Linux 실행 권한 보존).
- 각 패키지 폴더에 실행 스크립트를 넣는다.
  - Windows: `보기.bat`(일반 창), `PixelStreaming.bat`(시그널링 주소를 인자나 맨 위 변수로 바꿀 수 있게).
  - Linux: `view.sh`, `pixelstreaming.sh`(같은 방식), `check-nullrhi.sh`(화면 없는 서버용 `-nullrhi` 점검).
- 패키지 폴더 맨 위에 `테스트_안내.md`를 넣는다: 실행 방법, 주소 입력→보기, 시작(점검·보고서)과 보기의 차이, Pixel Streaming 시험 순서(시그널링 서버 실행 → 패키지 실행 → 브라우저 접속 주소), Linux 서버에서 필요한 것(GPU 드라이버·Vulkan, 실행 권한, 포트), 로그·보고서 위치, 계정이 든 주소 취급 주의. 비밀값·사내 주소는 쓰지 않는다.
- FFmpeg LGPL 라이선스·출처 파일이 패키지에 들어 있는지 확인한다.

## 4. 시험

시험용 RTSP는 이전처럼 로컬 mediamtx와 FFmpeg 송출(`Temp\cctv\dl`에 남겨 둔 도구)로 만든다. 없으면 멈추고 보고한다(새 다운로드 금지). Windows 일반 창 시험 전에는 `OpenInputDesktop`으로 잠금 해제를 확인하고, 잠겨 있으면 그 항목만 ENV_BLOCKED로 둔다.

| ID | 시험 | 합격 기준 |
|---|---|---|
| P1 | 단위 테스트 전체 | 기존 + 보기 모드 테스트 통과 |
| P2 | Windows 패키지 보기 모드 실제 입력 | 일반 창에서 주소 4개 입력 → 보기 → 4채널 재생 2분 이상 → 2열/4열 전환 → 중지. 스크린샷 검수. 16채널 보기 2분도 1회 |
| P3 | Windows Pixel Streaming | 시그널링 서버 + 패키지(PS 인자) 실행, 브라우저에서 화면 수신 확인. 브라우저 입력으로 주소 추가·보기·중지가 되는지 확인(가능한 자동화 도구가 없으면 화면 수신까지 확인하고 입력 확인은 `CLAUDE_CHECK`로 남긴다. Claude가 직접 확인한다). PS 중 CCTV 4채널 FPS 기록 |
| P4 | Linux 패키지 | Docker를 기존 방식으로 써서 `check-nullrhi.sh`와 `-CctvView -nullrhi` 4채널 2분 재생 확인. Docker Desktop이 꺼져 있으면 켜서 쓰고 끝나면 원래 상태로 둔다. 기존 이미지·컨테이너는 바꾸지 않는다. Linux 화면·Pixel Streaming은 이 PC에서 ENV_BLOCKED로 기록 |
| P5 | 압축 파일 | zip/tar.gz를 새 임시 폴더에 풀어 실행 파일·스크립트·라이선스·안내서가 있는지, Linux 실행 권한이 남아 있는지 확인 |
| P6 | 정리 | 시험 중 만든 임시 산출물 정리(01Z 기준). `C:\CCTV-TestPackages\2026-11-08\`는 남긴다. 시그널링 서버·패키지 등 내가 띄운 프로세스 종료 |

P3에서 Claude가 확인할 수 있도록, 끝낼 때 시그널링 서버와 PS 패키지를 다시 띄우는 방법(명령 두 줄)을 RESULT.md 맨 위에 적는다. 프로세스는 띄워 둔 채로 끝내지 않는다.

## 5. 문서·산출물

- README의 현장 점검 절과 `docs/INTEGRATION_GUIDE.md` 9.1절에 보기 모드와 테스트 패키지 위치를 짧게 추가한다.
- 의미 단위 커밋(`CCTV-01P`). 이 지시서도 커밋한다. 패키지·zip은 커밋하지 않는다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01P/RESULT.md`, `result.json`, `progress.md`(수시 갱신).

완료 보고: 보기 모드 동작 / 패키지 위치와 구성 / P1~P6 판정 / Pixel Streaming 입력 설정과 근거 / 미수행·제한.

## 다운로드 승인 (2026-11-08 사용자 승인)

`Temp\cctv\dl`의 FFmpeg·mediamtx 파일이 사라져(폴더만 남음) 첫 실행이 멈췄다. 사용자가 다음 두 가지 다운로드를 승인했다. 이 둘 외의 다운로드는 여전히 금지다.

- FFmpeg 8.1 Windows 64비트 LGPL 빌드: [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) 공식 GitHub 릴리스.
- mediamtx Windows amd64 최신 안정 릴리스: bluenviron/mediamtx 공식 GitHub 릴리스.

받은 파일은 `C:\CCTV-TestPackages\tools\` 아래 각자 새 폴더에 푼다(Temp에 두지 않는다). 릴리스의 체크섬이 있으면 확인하고, 받은 URL·크기·SHA256을 RESULT.md에 기록한다. 두 파일이 합쳐 300MB를 넘으면 받기 전에 멈추고 보고한다. 이 도구들은 정리 대상이 아니다(다음 시험에서 재사용).
