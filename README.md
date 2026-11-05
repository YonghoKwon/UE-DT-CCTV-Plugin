# DTCctv

UE 5.3 프로젝트에서 RTSP CCTV를 표시하는 Windows·Linux용 C++ 플러그인입니다. DTCore나 운영 프로젝트에 의존하지 않으며 플러그인 안에 블루프린트·맵·위젯 에셋을 넣지 않습니다. 오디오는 지원하지 않습니다.

CCTV-01E에서 최신 Windows·Linux 패키징과 단위 테스트14개, 게임144/60/30fps 조건을 확인했습니다. 소비 여유가 있는 버스트는 작업 스레드 역압으로 보존하고 지연 p95와 대체율을 함께 판정합니다. Close/Open30회는 Private·GPU 조건을 통과했지만 원시 핸들·총 스레드 증가로 FAIL입니다. Linux Vulkan은 ENV_BLOCKED입니다. 게임60fps 소크는 유효3604.2초·표시59.935fps·최악p9535ms·대체율0.106%로 승인된 스레드 기준까지 통과했습니다. 전체 판정은 E4 실패 때문에 FAIL입니다.

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

**기존 FFMPEGMedia 플러그인과 함께 사용하지 말 것.** 이 조합은 시험 대상에서 제외했습니다. Pixel Streaming을 활성화한 Windows·Linux 빌드의 마운트와 16채널 재생을 확인했습니다. 판정 범위는 **"플러그인 로드 공존만 확인, 실제 스트리밍 동시 부하는 미검증"**입니다. Linux는 nullrhi 소프트웨어 디코딩 범위입니다.

## Linux 드라이버와 서버 점검

NVDEC에는 `libcuda.so.1`뿐 아니라 `libnvcuvid.so.1`이 필요합니다. 컨테이너의 기본 compute/utility capability만으로 video 라이브러리가 노출되지 않을 수 있습니다. 이 PC에서는 `--gpus all -e NVIDIA_DRIVER_CAPABILITIES=all`로 CUDA4/SW12 디코딩을 확인했습니다. `libnvidia-encode.so.1`은 실제 NVENC 인코딩을 사용할 때도 확인해야 합니다.

Vulkan 렌더링에는 사용할 GPU와 호환되는 ICD와 그 종속 라이브러리, UE 기능 수준을 만족하는 드라이버가 필요합니다. `LD_LIBRARY_PATH`, `VK_ICD_FILENAMES`/`VK_DRIVER_FILES`, `/usr/share/vulkan/icd.d`, `ldd`와 `vulkaninfo --summary`를 확인하십시오. WSL2의 CUDA/video 노출이 Vulkan 지원을 보증하지 않습니다. 현재 로컬 이미지의 기본 dzn, lavapipe 명시·SM5·CPU 장치 허용과 엔진 제공 `-SkipVulkanProfileCheck` 시험에서도 호환 ICD 초기화에 실패했습니다. 이 옵션은 진단용으로만 썼으며 운영 합격이나 드라이버 지원을 대신하지 않습니다.

실패 지점은 `CheckVulkanProfile`/`RHIInit`으로 DTCctv 모듈 로드 전입니다. 플러그인 코드로 엔진 초기화 실패를 막을 수 없습니다. `-nullrhi`에서는 GPU 렌더링을 건너뛰고 디코딩만 수행합니다. Linux 렌더링 판정은 현재 `ENV_BLOCKED`이며, 정상 Linux GPU 서버에서 확인해야 합니다.

운영 서버에서는 같은 디렉터리의 두 점검 파일을 사용합니다.

```bash
bash TestHost/Scripts/linux_server_check.sh /path/to/Linux "$RTSP_URL"
```

입력은 패키지 경로와 RTSP URL이며 Python3 표준 라이브러리만 필요합니다(`python3` 또는 RHEL의 `/usr/libexec/platform-python`). 새 하네스의 `-CctvUrlEnv`로 인증 URL을 전달해 UE 명령행에 계정을 넣지 않고, 로그 URI와 Authorization을 가립니다. 임시 사용자 디렉터리는 자체 생성한 경로만 정리합니다. 기본 점검은 16채널·60초 이상·CUDA4/SW12·58fps·손실/재연결/프레임 대체0·1920x1080 비검은 타일 표본을 확인해 PASS/FAIL을 기록합니다. 입력 카메라는 1080p60 조건이어야 합니다. `--software-only`는 로더·SW 디코딩 범위만 확인하며 GPU 렌더링 합격을 뜻하지 않습니다. 이 PC에서 구문·가림 처리와 실제 SW 실행(69.3초, decode59.984fps, 16Playing)을 확인했습니다. 기본 GPU 점검의 실제 서버 실행은 NOT_RUN입니다.

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
| 카메라 | `MinReconnectDelaySeconds`, `MaxReconnectDelaySeconds` | 0.5초, 2초. 지수 대기 기준값에 최대 25% 지터가 더해짐. 명시적으로 지정한 값은 유지 |
| 프로젝트 `UDTCctvSettings` | `MaxConcurrentConnects` | 8: 동시 연결 시도 상한 |
| 프로젝트 | `MaxQueuedPackets` | 채널당 120. 초과하면 다음 키프레임까지 버림 |
| 프로젝트 | `MaxHardwareDecodeStreams` | 4: 프로세스 내 HW 슬롯 예약 상한. 나머지는 소프트웨어 |
| 프로젝트 | `SoftwareDecodeThreads` | 채널당 FFmpeg 스레드 설정 2 |
| 프로젝트 | `SubStreamMaxWidth` | 960px 미만이면 서브 선택. 경계를 넘은 상태가 2초 유지되면 전환 |

기본 재연결 상한2초는 장애 복귀 후 다음 시도까지의 대기를 줄입니다. 실패가 빨리 반환되는 꺼진 카메라에는 약2초 대기(최대25% 지터 추가) 뒤 계속 재시도하므로 서버·카메라의 실패 요청 수가 늘어납니다. 연결·읽기 기한은 이 대기와 별도입니다. 카메라의 동시 연결 제한이 작으면 `MaxReconnectDelaySeconds`를 늘리고 `MaxConcurrentConnects`도 환경에 맞춥니다. 실제 장애가 없는 정상 연결은 재연결하지 않습니다.

프로젝트 설정의 `DT CCTV` 또는 `DefaultGame.ini`의 `[/Script/DTCctv.DTCctvSettings]`에서 전역 값을 지정합니다. 작은 타일에는 실제 카메라의 서브 스트림을 쓰고, 큰 화면에는 메인을 쓰는 구성을 권장합니다. 동일 주소를 `SubUrl`에 반복하는 것은 디코딩 부하를 줄이지 않습니다.

새 세션이 첫 프레임을 만들 때까지 기존 세션을 유지한 뒤 교체합니다. 표시 크기는 원본보다 커지지 않으며, 10% 이상 크기 변화가 0.25초 유지될 때 렌더 타깃을 조정합니다. 출력 폭·높이가 모두 원본의 절반 이하이면 업로드 전에 CPU에서 축소합니다. 하드웨어 디코딩도 V1에서는 시스템 메모리로 내려받아 업로드하므로 HW 채널 수를 무조건 늘리면 성능이 나빠질 수 있습니다.

출력 감시는 읽기 루프에서 첫 키프레임 패킷을 기다리는 단계와 디코더 출력을 기다리는 단계를 구분합니다. 첫 키프레임 전에는 디코더 고장으로 판단하거나 소프트웨어 디코딩을 강제하지 않고, 30초 동안 키프레임이 없으면 일반 재연결합니다. 키프레임 수신 후 첫 출력이 없으면 그 수신 시점부터 `max(ReadTimeoutSeconds, 10초)`를 적용하며 HW 세션은 소프트웨어로 재시도합니다. 재생이 시작된 뒤에는 마지막 출력부터 `ReadTimeoutSeconds`로 감시합니다. 수신 자체가 끊기는 읽기 기한은 별도로 적용합니다. 판단 순수 함수의 경계값 단위 테스트는 CCTV-01C에서 성공했으며 실제 긴 GOP 카메라 시험은 남아 있습니다.

카메라 코덱은 연결 시 다음 로그 형식으로 확인합니다. FPS는 스트림 메타데이터의 값이며 측정 FPS와 다릅니다.

```text
LogDTCctv: ... 코덱=h264 해상도=1920x1080 FPS=60.00 디코딩=cuda
```

URL은 `DTCctvMaskUrlCredentials`로 계정을 가립니다. 운영 코드에서도 원본 URL을 별도로 로그에 쓰지 마십시오. `PacketsDropped`는 과부하 패킷 손실이고 `FramesSuperseded`는 최대 8장의 표시 대기 프레임을 유지하면서 오래된 프레임을 대체한 수입니다. `QueuedFrames`, `PeakQueuedFrames`, `RenderCommandsInFlight`로 압축 패킷 큐와 구분해 확인합니다. `FramesDisplayed`는 렌더 명령 등록 수이며 GPU 완료·실제 모니터 표시를 보증하는 계측은 아닙니다. `-nullrhi`에서는 표시 수를 늘리지 않습니다.

## frame age와 게임 FPS

디코더가 프레임을 반환한 직후부터 렌더 명령 등록까지를 `FrameAgeMeanMs`, `FrameAgeP95Ms`, `FrameAgeMaxMs`로 기록합니다. HW 다운로드와 CPU 변환을 포함하며 네트워크 전송·디코더 입력 대기·GPU 완료·모니터 표시 시간은 제외합니다. 채널별 모든 제출 프레임을1ms 구간 히스토그램에 집계하고 p95는 구간 상한을 사용합니다. TestHost는10초 워밍업 뒤 이 지연 표본만 초기화하며 기존 손실·재연결·프레임 카운터는 유지합니다.

표시 대기열은 최대8장입니다. 짧은 버스트는 FIFO로 보존합니다. 대기열이 비지 않는 상태가100ms 이상 지속되거나 가장 오래된 프레임의 age가100ms를 넘으면, 오래된 프레임을 건너뛰어40ms 안쪽의 프레임부터 다시 표시합니다. 최신 프레임도 오래됐다면 그 실제 age를 기록합니다. 건너뛴 프레임과8장 초과 대체는 모두 기존 `FramesSuperseded`에 포함합니다. 재생 속도·프레임 순서를 바꾸지 않으며 게임 FPS보다 높은 입력 FPS의 모든 프레임을 표시할 수는 없습니다.

소비 여유가25% 이상이면 짧게 몰려온 프레임을750ms까지 순서대로 소진하도록 유예합니다. 게임 틱 주기의 평활값과 메타데이터·측정 디코딩 FPS를 사용하되 유예는 유한합니다. 큐가 꽉 차면 디코딩 작업 스레드만 공간을 기다리며 게임 스레드는 기다리지 않습니다. 남은 압축 패킷도 기존120개 상한 안에서 대기합니다. 유예가 끝나거나 소비 여유가 없으면 기존 지연 정책과 대체 집계를 적용합니다. 한 틱에 등록하는 프레임 수는 늘리지 않습니다.

CCTV-01F는 이 대기에 소비자 활동 임대를 추가합니다. `TakeFrame` 호출이350ms 안에, 실제 프레임 소비가750ms 안에 있었을 때만 기다릴 수 있습니다. 0.3초 버스트 보존과60fps 패킷 큐 여유를 함께 고려한 경계입니다. GT가 멈추면 오래된 참 값만으로 기다리지 않고, 렌더 진행 상한으로 건너뛸 때는 즉시 소비 불가를 알리고 대기를 깨웁니다. 기존 패킷 큐·프레임 큐 상한과 `FramesSuperseded` 집계는 유지합니다. `PeakQueuedPackets`, `PacketKeyWaits`, `PacketKeyWaitSeconds`로 압축 큐 고수위와 재동기화 대기를 구분합니다.

대기 버퍼는8장과24MiB 예산 중 먼저 도달하는 상한으로 제한합니다. 단일 프레임이24MiB를 넘으면 그 프레임1장까지만 유지합니다. HD8장·4K2장 예산과 소비 여유가 있는16개 생산 버스트의 FIFO 보존을 단위 시험으로 확인합니다. 이 정책은30fps 보조 측정의 일괄 디코딩에서85건 대체가 나온 뒤 보완했습니다. 실패한 이전 코드 결과도 보존합니다.

`QueuedFrameBytes`와 `PeakQueuedFrameBytes`는 대기 프레임의 실제 AVBuffer 참조 크기를 보수적으로 합산합니다. 공유 참조는 중복 계산할 수 있으며 디코더 내부·렌더 명령·드라이버 전체 메모리의 합은 아닙니다. 같은 스트림을 다시 열 때 알고 있는 표시 크기를 먼저 전달해 원본 크기 프레임에서 축소 프레임으로 바뀌는 임시 할당을 줄입니다. 원본 비율·원본 해상도 메타데이터는 유지합니다. 코덱 참조가 모두 끝난 공유 HW 장치 캐시는 읽기 스레드에서 해제하므로 재개할 때 장치 초기화 비용이 다시 들 수 있습니다.

16채널·100초 실행에서 시작10초를 제외한80초 이상을 판정합니다. 아래는 CCTV-01E의 합격 기준이며 최종 코드의 결과는 `Saved/Reports/CCTV-01E/RESULT.md`에 기록합니다.

| 게임 FPS / 영상 FPS | 표시·지연 기준 | 대체 기준 |
|---|---|---|
| 144 / 60 | 표시≥58, 채널별 p95≤34ms | 0 |
| 60 / 60 | 디코딩·표시≥58, 채널별 p95≤50ms, 손실·재연결0 | 디코딩 프레임 대비1% 이하 |
| 30 / 60 | 표시≥28, 채널별 p95≤100ms, 손실·재연결0 | 불가피한 대체 수치 기록 |
| 60 / 30 | 표시≥29, 채널별 p95≤50ms | 0 |

하네스의 `-CctvStallAt=30 -CctvStallSeconds=2`는 시험용으로 게임 틱만2초 멈춥니다. 수신·디코딩은 계속하며, 정지 종료 뒤 새 제출 표본의 age가 기준 안으로 돌아온 관측 상한이2초 이하여야 합니다. 강제 정지에 따른 대체 수는 정상 재생의 대체율과 구분해 보존합니다.

## 검증 상태와 보류 시험

2026-11-06 CCTV-01E의 최종 C++는 `e4a3eb8`입니다. Editor 빌드와 전체 단위14개가 성공했고(일반13·경고 포함 성공1), 새 Win64·Linux 패키지를 만들었습니다. FIFO·생산 대기·HD8장/4K2장 바이트 예산·디코더 반환 시각을 포함해 검증했습니다. FFmpeg 바이너리와 셰이더 소스는 변경하지 않았습니다.

보정한 수정 전 FIFO8의 최악 채널 p95는 게임144/영상60에서11ms,60/60에서91ms,30/60에서143ms,60/30에서19ms였습니다. 처음 CPU 변환 뒤부터 측정한9회는 원수치를 보존하고 최종 합격 근거에서 제외했습니다. 이후 디코더 반환 직후 기준의 격리 패키지로 수정 전4조건을 다시 측정했습니다.

최신 코드의4조건은 모두 통과했습니다. 최악 p95는11/36/82/19ms, 표시는59.961/59.906/29.989/29.988fps입니다. 144/60과60/30의 대체는0,60/60은0.1774%입니다. 별도30fps 보조 측정도 대체0을 확인했습니다. 0.3초 정지 버스트를8장 상한 안에서 대체0으로 보존했고 최대 age312ms도 기록했습니다. 2초 정지 회복은 별도 게이트에서 확인했습니다.

Windows 기본 패키지의 1920×1080 16타일 PNG를 직접 검수했습니다. Linux RHEL8의 SW16과 CUDA4/SW12 디코딩, 운영 점검 스크립트 software-only, Windows·Linux PS 마운트 공존을 최신 패키지로 실행했습니다. Docker NVDEC에는 `--gpus all -e NVIDIA_DRIVER_CAPABILITIES=all`을 사용했습니다. Vulkan은 엔진 RHI 초기화에서 `VK_ERROR_INCOMPATIBLE_DRIVER`·종료139로 끝나 렌더링을 입증하지 못했습니다.

크기5시나리오 PNG를 직접 표본 검수했고 극단 확대 흐림은 저장시각 상한 최대0.754초로 회복했습니다. 연속 PNG 캡처 부하가 있는 표시는 약54fps이므로 기본 FPS 결과와 구분합니다. PNG 없는 별도 subset 측정에서 미변경12채널 표시는59.940fps, 기본59.913fps였습니다. 서버 재시작 후 전16채널 복구는2.672초로5초 목표를 통과했습니다.

Close/Open30회 마지막10회의 Private 기울기는0.782MiB/회, GPU 첫끝 차이는0입니다. 평면 재생성32→32와 RHI 스트리밍/비스토리밍 텍스처 할당량도 고정입니다. 그러나 원시 핸들+2·총 스레드+2로 자원 게이트는 FAIL입니다. 실행파일86/CRT16/NVIDIA 모듈 수는 일정하고 ntdll1~4 변동을 관측했습니다. 이 실패를 누수 없음이나 전체 자원 회복의 합격으로 바꾸지 않았습니다.

사용자는 소크에 한해 처음10분 이후 실행파일·CRT·NVIDIA 시작 TID 집합 일정, OS 모듈에서만 변동, 마지막10분 총수 최대가10~20분 최대를 넘지 않는 기준을 승인했습니다. 이전 총수 min/max도 함께 기록합니다. 이 변경을 Close/Open 기준으로 확장하지 않았습니다. 게임60fps 소크는 유효3604.2초, decode59.9989/display59.9355fps, 최악채널p9535ms, 대체3672건(0.1061%), 패킷 손실·재연결·오류0으로 통과했습니다. 실행파일·CRT·NVIDIA TID 집합은 일정하고 변동은ntdll에만 있었으며, 총수 최대는10~20분123개/최종10분122개입니다. 이전 총수 일정 기준은121~123으로 FAIL도 기록했습니다. 10분 이후 Private는+6.449MiB(회귀+2.428MiB/h), GPU Dedicated는−0.0039MiB입니다. Working Set의 큰 감소를 Private 해제나 누수 없음의 증거로 사용하지 않았습니다. 최종 판정은 `Saved/Reports/CCTV-01E/RESULT.md`에 있습니다. 실제 카메라·회사 수용·72시간·실제 Pixel Streaming 동시 인코딩은 미검증입니다.

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

`fault_test.ps1`도 기본적으로 실행하지 않으며, 실행 시 직접 시작한 PID의 생성 시각과 실행 경로를 확인한 후에만 종료합니다. CCTV-01C·01D에서 실행기와 자원 수집·자체 프로세스 및 컨테이너 정리 경로를 실제 사용했습니다.

측정용 Windows 게임은 숨김 창 대신 `-RenderOffScreen`으로 실행하도록 준비했습니다. `WindowStyle Hidden`은 mediamtx·ffmpeg에만 적용합니다. 첫 Windows 측정은 워밍업 뒤 `output_w > 0`과 최종 PNG의 비검은 픽셀 표본을 확인하며, 0 크기·검은 화면·PNG 읽기 실패는 `INVALID`로 기록합니다. 이 확인은 오프스크린 렌더링이 실제로 동작하는지 점검하기 위한 것이며, 화면 품질과 전환 깜빡임의 직접 검수를 대신하지 않습니다. CCTV-01C에서 오프스크린·1920×1080 PNG와 양수 출력 크기를 확인했습니다.

기본 실행기 결과는 `Saved/Reports/CCTV-01/pending-<시각>/`, 큰 로그는 `Saved/Logs/CCTV-01/`에 둡니다. CCTV-01D 결과·로그는 각각 `Saved/Reports/CCTV-01D/`, `Saved/Logs/CCTV-01D/`에 있습니다. 패키지는 저장소 밖 기존 임시 폴더를 사용합니다. Windows 16채널 1080p60 기준은 60초 이상, 평균 디코딩·표시 각각 58fps 이상, 패킷 손실·재연결 0으로 유지합니다. nullrhi는 디코딩 범위만 판정하고 렌더링과 구분합니다. CCTV-01D의 최종 게이트 판정에는 검수 대기 항목이 없습니다.

## 한계

- Linux NVDEC 디코딩은 nullrhi CUDA4/SW12로 검증했습니다. Vulkan YUV 렌더링은 드라이버 초기화 실패로 ENV_BLOCKED입니다. PS 로드 공존은 CCTV-01C 소프트웨어 실행 범위의 증거입니다.
- 위 측정은 로컬 합성 입력 범위이며 Linux 렌더링과 실카메라·회사 수용 검증으로 확대 해석하지 않습니다.
- `ps_coexist` 결과는 플러그인 로드 공존 범위입니다. 실제 Pixel Streaming 클라이언트 연결·영상 수신과 NVENC 인코딩 중 RTSP 16채널 동시 부하는 미검증입니다. 실행기 결과의 `known_limits`와 `user_checks`에도 이 한계를 남깁니다.
- 렌더 명령 대기 중 강제 GC와 실제 GPU 장치 오류 복구의 별도 재현 시험은 남아 있습니다.
- 극단 확대 직후 일시적 흐림은 최종 PNG 표본에서 최대0.805초 이내 회복했습니다. PNG 표본은 모든 렌더 프레임의 무깜빡임을 보증하지 않습니다.
- 서브 전환 중 기존·새 연결이 잠시 공존하며 새 주소가 실패하면 전환 대기 세션이 재연결을 계속합니다.
- 프레임 대기열은 최대 8장입니다. 카메라보다 낮은 표시 FPS에서는 모든 프레임을 화면에 표시할 수 없습니다. 측정 무손실이 임의 부하의 무손실 보증은 아닙니다.
- 오디오·탐색·시계 동기화와 GPU 간 직접 표면 공유는 지원하지 않습니다.

사용자 확인 항목: Windows·Linux에서 실제 시그널링 서버·클라이언트를 연결해 영상을 수신하고, NVENC 인코딩 활성 증거와 RTSP 16채널 동시 재생의 FPS·손실·GPU 자원 추세를 확인해야 합니다. 플러그인 로드 공존 합격만으로 이 동시 부하까지 합격했다고 해석하지 않습니다.

설계와 미검증 항목은 [docs/DESIGN.md](docs/DESIGN.md), 이번 작업 결과는 `Saved/Reports/CCTV-01E/RESULT.md`를 참조하십시오.
