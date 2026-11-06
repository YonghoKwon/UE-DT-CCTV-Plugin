# 기존 FFMPEGMedia 설치 공존 재시험

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
