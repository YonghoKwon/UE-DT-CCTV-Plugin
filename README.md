# DTCctv

UE 5.3 프로젝트에서 RTSP CCTV를 표시하는 Windows·Linux용 C++ 플러그인입니다. DTCore나 운영 프로젝트에 의존하지 않으며 플러그인 안에 블루프린트·맵·위젯 에셋을 넣지 않습니다. 오디오는 지원하지 않습니다.

현재 CCTV-01B의 검수 결함 수정까지 진행했습니다. **이번 C++ 변경은 빌드 미검증이며 CCTV-01의 패키지 재생·장애·크기 변경·장시간·Pixel Streaming 공존 게이트는 미수행입니다.** 사용자 요청으로 UE·빌드·GPU·Docker·송출 도구 실행과 프로세스 종료가 중단된 상태입니다.

## 프로젝트에 적용

1. Git LFS가 설치된 환경에서 저장소를 받고 `git lfs pull`로 바이너리 원본을 준비합니다. DLL·LIB·버전 접미사 `.so`가 작은 LFS 포인터 텍스트인 상태로 빌드하지 않습니다.
2. 운영 프로젝트의 `Plugins/DTCctv/`에 `DTCctv.uplugin`, `Source/`, `Shaders/`, 이 README를 복사합니다. `Source/ThirdParty/DTCctvFFmpeg/LICENSE.txt`와 출처 문서도 함께 유지합니다. `TestHost/`, `Binaries/`, `Intermediate/`, `Saved/`, `.git/`는 운영 프로젝트로 복사하지 않습니다.
3. 운영 프로젝트의 `.uproject`에 다음 항목을 추가합니다. 프로젝트에 이미 `Plugins` 배열이 있으면 그 배열에 합칩니다.

```json
"Plugins": [
  { "Name": "DTCctv", "Enabled": true }
]
```

4. C++ 호출 모듈의 `.Build.cs`에 `DTCctv`를 의존성으로 추가하고 해당 프로젝트를 빌드합니다. 플러그인의 FFmpeg 헤더나 외부 모듈을 운영 코드에서 직접 포함할 필요는 없습니다.

Windows는 FFmpeg DLL 5개, Linux는 SONAME 파일 5개를 실행 바이너리 옆에 스테이징합니다. Linux의 확장자 `.so` 파일은 버전 파일을 가리키는 텍스트 링커 스크립트입니다. 이를 바이너리로 교체하거나 버전 라이브러리를 빼면 안 됩니다. 자세한 버전·LGPL 정보는 [동봉 FFmpeg 설명](Source/ThirdParty/DTCctvFFmpeg/README.md)에 있습니다.

**기존 FFMPEGMedia 플러그인과 함께 사용하지 말 것.** 이 조합은 시험 대상에서 제외했습니다. Pixel Streaming은 TestHost에서 활성화해 시험을 준비했으며 Windows·Linux의 실제 심볼 공존은 아직 확인하지 않았습니다. 실행기의 `ps_coexist` 판정 범위는 **"플러그인 로드 공존만 확인, 실제 스트리밍 동시 부하는 미검증"**입니다. 현재는 그 로드 공존 시험도 미수행입니다. 기존 합격 기준인 PixelStreaming 활성화 빌드의 시작과 16채널 재생은 유지합니다.

## UMG에서 사용

운영 프로젝트에 부모 클래스가 `UserWidget`인 위젯 블루프린트를 만듭니다.

| 항목 | 설정 |
|---|---|
| 팔레트 | `DT CCTV` → `DTCctvView` |
| 위젯 이름 | `CctvView`, `Is Variable` 켜기 |
| `bAutoPlay` | 런타임에 주소를 넣는 경우 끄기 |
| `Stretch` | `ScaleToFit`: 비율 유지, `Fill`: 슬롯에 맞춰 늘리기 |
| 슬롯 | Canvas·SizeBox 등 운영 UI의 크기와 앵커로 배치 |

하나의 생성·연결 경로는 다음과 같습니다.

1. 운영 위젯의 `Event Construct`에서 런타임 설정으로 `Make DTCctvStreamConfig`를 구성합니다. `Url`과 선택적인 `SubUrl`을 넣고 `Transport=Tcp`, `DecodeMode=Auto`로 시작합니다. 계정이 있는 주소를 에셋이나 소스에 저장하지 않습니다.
2. `CctvView`의 `Config`를 설정한 뒤 `Play`를 호출합니다.
3. `CctvView → GetStream`으로 얻은 객체의 `OnStateChanged`에 운영 위젯의 이벤트를 바인딩합니다. 최초 표시에는 `GetState`를 읽습니다.
4. 운영 위젯의 `Event Destruct`에서 `CctvView → Stop`을 호출합니다. Slate 리소스 해제 경로에서도 닫지만 명시적인 화면 수명 관리를 권장합니다.

`Stop` 없이 위젯만 제거하면 GC 전까지 재생이 계속됩니다. 제거 즉시 재생을 끝내려면 `Stop`을 명시적으로 호출하십시오. GC 중 월드를 찾지 못해도 스트림의 Outer인 서브시스템에서 제거하도록 했으며, 스트림이나 소유 서브시스템이 같은 GC에서 파괴 중이면 닫기·목록 접근을 건너뜁니다. 이 수명 경로의 실제 UE 검증은 남아 있습니다.

**재연결 상태 표시는 운영 위젯의 책임입니다.** `DTCctvView`는 영상만 표시하며 자체적인 재연결 문구·스피너·실패 배너를 만들지 않습니다. `Connecting`, `Playing`, `Reconnecting`, `Idle`을 운영 UI의 텍스트나 오버레이에 연결합니다. `Reconnecting`일 때 영상 브러시를 지우지 않으면 마지막 프레임을 계속 보여 줄 수 있습니다.

## C++·블루프린트 API

모든 공개 함수는 게임 스레드에서 호출합니다. 연결·수신·세션 정리는 작업 스레드와 지연 해제 경로를 사용합니다. 모듈 종료에서는 남은 세션의 종료를 기다립니다.

| 객체 | API | 동작 |
|---|---|---|
| `UDTCctvView` | `Config`, `Play`, `PlayUrl`, `Stop`, `GetStream` | UMG 영상과 스트림 수명 연결 |
| `UDTCctvStream` | `Open`, `Close`, `IsOpen` | 비동기 연결 요청과 닫기. `IsOpen`은 세션 존재 여부이며 실제 재생은 상태로 확인 |
| `UDTCctvStream` | `GetTexture` | UMG·머티리얼에 연결할 렌더 타깃. 같은 스트림 객체에서 재사용 |
| `UDTCctvStream` | `GetState`, `OnStateChanged`, `GetStats` | 연결 상태·통계 조회와 상태 알림 |
| `UDTCctvStream` | `ReportViewSize` | 표시 크기(px) 보고. `DTCctvView`는 Slate 크기로 자동 보고 |
| `UDTCctvStream` | `SetUseSubStream` | 수동 선택. 이후 해당 Open 수명 동안 자동 선택을 중지. 다시 `Open`하면 자동 선택 초기화 |
| `UDTCctvSubsystem` | `OpenStream`, `CloseStream`, `CloseAllStreams`, `GetStreams` | GameInstance 수명 동안 스트림 소유 |

```cpp
#include "DTCctvStream.h"
#include "DTCctvSubsystem.h"

// 호출 객체에는 UPROPERTY(Transient) TObjectPtr<UDTCctvStream> Stream; 을 둔다.
FDTCctvStreamConfig Config;
Config.Url = RuntimeCameraUrl;
Config.SubUrl = RuntimeSubStreamUrl;
UDTCctvSubsystem* Cctv = GetGameInstance()->GetSubsystem<UDTCctvSubsystem>();
Stream = Cctv->OpenStream(Config);
Stream->ReportViewSize(FVector2D(480.0, 270.0));
// Stream->GetTexture()를 운영 머티리얼·브러시에 연결한다.
// 화면을 닫을 때 Cctv->CloseStream(Stream); 을 호출한다.
```

## 설정과 권장 구성

| 범위 | 이름 | 기본값·의미 |
|---|---|---|
| 카메라 `FDTCctvStreamConfig` | `Url`, `SubUrl` | 메인 주소, 선택적인 서브 주소 |
| 카메라 | `Transport`, `DecodeMode` | TCP, Auto. Auto는 CUDA/NVDEC → Windows D3D11VA 또는 Linux VAAPI → 소프트웨어 순서 |
| 카메라 | `ConnectTimeoutSeconds`, `ReadTimeoutSeconds` | 각각 5초. 연결·분석 기한, 읽기·출력 감시 기한 |
| 카메라 | `MinReconnectDelaySeconds`, `MaxReconnectDelaySeconds` | 0.5초, 10초. 지수 대기 기준값에 최대 25% 지터가 더해짐 |
| 프로젝트 `UDTCctvSettings` | `MaxConcurrentConnects` | 8: 동시 연결 시도 상한 |
| 프로젝트 | `MaxQueuedPackets` | 채널당 120. 초과하면 다음 키프레임까지 버림 |
| 프로젝트 | `MaxHardwareDecodeStreams` | 4: 프로세스 내 HW 슬롯 예약 상한. 나머지는 소프트웨어 |
| 프로젝트 | `SoftwareDecodeThreads` | 채널당 FFmpeg 스레드 설정 2 |
| 프로젝트 | `SubStreamMaxWidth` | 960px 미만이면 서브 선택. 경계를 넘은 상태가 2초 유지되면 전환 |

프로젝트 설정의 `DT CCTV` 또는 `DefaultGame.ini`의 `[/Script/DTCctv.DTCctvSettings]`에서 전역 값을 지정합니다. 작은 타일에는 실제 카메라의 서브 스트림을 쓰고, 큰 화면에는 메인을 쓰는 구성을 권장합니다. 동일 주소를 `SubUrl`에 반복하는 것은 디코딩 부하를 줄이지 않습니다.

새 세션이 첫 프레임을 만들 때까지 기존 세션을 유지한 뒤 교체합니다. 표시 크기는 원본보다 커지지 않으며, 10% 이상 크기 변화가 0.25초 유지될 때 렌더 타깃을 조정합니다. 출력 폭·높이가 모두 원본의 절반 이하이면 업로드 전에 CPU에서 축소합니다. 하드웨어 디코딩도 V1에서는 시스템 메모리로 내려받아 업로드하므로 HW 채널 수를 무조건 늘리면 성능이 나빠질 수 있습니다.

출력 감시는 읽기 루프에서 첫 키프레임 패킷을 기다리는 단계와 디코더 출력을 기다리는 단계를 구분합니다. 첫 키프레임 전에는 디코더 고장으로 판단하거나 소프트웨어 디코딩을 강제하지 않고, 30초 동안 키프레임이 없으면 일반 재연결합니다. 키프레임 수신 후 첫 출력이 없으면 그 수신 시점부터 `max(ReadTimeoutSeconds, 10초)`를 적용하며 HW 세션은 소프트웨어로 재시도합니다. 재생이 시작된 뒤에는 마지막 출력부터 `ReadTimeoutSeconds`로 감시합니다. 수신 자체가 끊기는 읽기 기한은 별도로 적용합니다. 판단 순수 함수와 경계값 단위 테스트를 추가했으나 빌드·실행하지 않았습니다.

카메라 코덱은 연결 시 다음 로그 형식으로 확인합니다. FPS는 스트림 메타데이터의 값이며 측정 FPS와 다릅니다.

```text
LogDTCctv: ... 코덱=h264 해상도=1920x1080 FPS=60.00 디코딩=cuda
```

URL은 `DTCctvMaskUrlCredentials`로 계정을 가립니다. 운영 코드에서도 원본 URL을 별도로 로그에 쓰지 마십시오. `PacketsDropped`는 과부하 패킷 손실이고 `FramesSuperseded`는 최대 3장의 표시 대기 프레임을 유지하면서 오래된 프레임을 대체한 수입니다. `FramesDisplayed`는 렌더 명령 등록 수이며 GPU 완료·실제 모니터 표시를 보증하는 계측은 아닙니다. `-nullrhi`에서는 표시 수를 늘리지 않습니다.

## 검증 상태와 보류 시험

2026-11-05 중단 요청 전 기존 코드에서 Windows Editor 빌드, Windows·Linux Development Game 빌드와 패키징이 성공했습니다. 당시 단위 테스트는 5개 성공·실패 0이었습니다. 양쪽 패키지에 FFmpeg 파일 5개씩 포함됐고, 쿡된 Windows SM5 및 Linux Vulkan SM5·SM6 전역 캐시에서 `FDTCctvYuvPS`를 확인했습니다. Linux ELF는 `libavcodec.so.62` 등 버전 SONAME을 참조했습니다.

이는 **CCTV-01A 수정 전 증거**입니다. 추가한 `DTCctv.Policies.FrameMetadata` 테스트와 이번 하네스·Pixel Streaming 구성은 빌드·실행하지 않았습니다. 기존 패키지는 임시 `cctv/pkg` 폴더에 보존했으며 현재 소스를 반영한 패키지가 아닙니다. 실제 패키지 FPS, Linux RTSP, 장애 복구, 스크린샷, 60분 자원 추세의 측정값은 아직 없습니다. 목표 72시간 안정성 역시 미검증입니다.

| 준비한 하네스 옵션 | 시나리오·산출물 |
|---|---|
| `-CctvResizeScenario=drag` | 지정 채널의 크기를 매 틱 연속 변경 |
| `-CctvResizeScenario=extremes` | 32×18과 전체 화면 반복 |
| `-CctvResizeScenario=subset` | 일부 채널만 확대, 나머지 채널별 FPS 기록 |
| `-CctvResizeScenario=disconnected` | 장애 시험 스크립트의 단절 중에도 연속 크기 변경 |
| `-CctvSubUrl=...` | 크기 경계에 따른 자동 메인·서브 전환 |
| `-CctvResizeAffected=4`, `-CctvResizePeriod=12` | 변경 대상 수, 주기(초) |
| `-CctvCloseOpenCycles=5`, `-CctvCloseOpenInterval=10`, `-CctvCloseHold=2` | 전 채널 Close/Open 반복 |
| `-CctvShot=...`, `-CctvShotEvery=0.5` | 최종 PNG와 10초 이후 연속 PNG |
| `-CctvCsv=...` | 집계 CSV, 같은 이름의 `.channels.csv`와 `.events.csv` |

사용자가 UE 사용을 다시 허용한 뒤에만 `TestHost/Scripts/run_pending_gates.ps1 -Execute`를 실행합니다. 기본 동작은 계획 출력입니다. 실행기는 현재 코드의 Editor 빌드·단위 테스트·양 플랫폼 재패키징을 먼저 수행하고, 4~8번을 순서대로 진행하며 60분 시험을 마지막에 둡니다. 한 번에 한 빌드·게임·측정만 실행합니다. 상주하는 `UnrealTraceServer`는 바쁜 작업에서 제외합니다. 다른 Editor·빌드·쿡·셰이더 컴파일·시험 프로세스는 계속 거부하며, 기존 작업을 종료하지 않습니다.

`fault_test.ps1`도 기본적으로 실행하지 않으며, 실행 시 직접 시작한 PID의 생성 시각과 실행 경로를 확인한 후에만 종료합니다. 준비한 실행기와 자원 수집·정리 경로 자체의 실행 검증도 남아 있습니다.

측정용 Windows 게임은 숨김 창 대신 `-RenderOffScreen`으로 실행하도록 준비했습니다. `WindowStyle Hidden`은 mediamtx·ffmpeg에만 적용합니다. 첫 Windows 측정은 워밍업 뒤 `output_w > 0`과 최종 PNG의 비검은 픽셀 표본을 확인하며, 0 크기·검은 화면·PNG 읽기 실패는 `INVALID`로 기록합니다. 이 확인은 오프스크린 렌더링이 실제로 동작하는지 점검하기 위한 것이며, 화면 품질과 전환 깜빡임의 직접 검수를 대신하지 않습니다. 현재는 이 경로도 실행하지 않았습니다.

결과는 `Saved/Reports/CCTV-01/pending-<시각>/`, 큰 빌드·실행 로그는 `Saved/Logs/CCTV-01/`에 둡니다. 패키지는 저장소 밖 기존 임시 패키지 폴더를 사용합니다. Windows 16채널 1080p60 기준은 60초 이상, 평균 디코딩·표시 각각 58fps 이상, 패킷 손실·재연결 0으로 유지합니다. Docker GPU 불가 시 RHEL 8 소프트웨어 디코딩만 확인하고 렌더링·NVDEC는 미수행으로 남깁니다. 연속 PNG의 검은 화면·깜빡임·마지막 프레임 유지, 반복 전후 자원과 장시간 누수 여부는 직접 검수하기 전까지 `REVIEW_REQUIRED`입니다.

## 한계

- 패키지 재생과 Linux RHEL 8 실제 로드, Pixel Streaming 공존은 아직 검증하지 않았습니다.
- `ps_coexist` 결과는 플러그인 로드 공존 범위입니다. 실제 Pixel Streaming 클라이언트 연결·영상 수신과 NVENC 인코딩 중 RTSP 16채널 동시 부하는 미검증입니다. 실행기 결과의 `known_limits`와 `user_checks`에도 이 한계를 남깁니다.
- 렌더 명령 대기 중 GC, 해상도 변경, 장치 오류 복구, Close/Open 자원 회수는 실제 UE 시험이 필요합니다.
- 서브 전환 중 기존·새 연결이 잠시 공존하며 새 주소가 실패하면 전환 대기 세션이 재연결을 계속합니다.
- 프레임 대기열은 최대 3장입니다. 카메라보다 낮은 표시 FPS에서는 모든 프레임을 화면에 표시할 수 없습니다.
- 오디오·탐색·시계 동기화와 GPU 간 직접 표면 공유는 지원하지 않습니다.

사용자 확인 항목: Windows·Linux에서 실제 시그널링 서버·클라이언트를 연결해 영상을 수신하고, NVENC 인코딩 활성 증거와 RTSP 16채널 동시 재생의 FPS·손실·GPU 자원 추세를 확인해야 합니다. 플러그인 로드 공존 합격만으로 이 동시 부하까지 합격했다고 해석하지 않습니다.

설계와 미검증 항목은 [docs/DESIGN.md](docs/DESIGN.md), 이번 작업 결과는 `Saved/Reports/CCTV-01B/RESULT.md`를 참조하십시오.
