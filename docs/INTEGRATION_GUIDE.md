# DTCctv 운영 프로젝트 적용 안내서

DTCore 기반 UE 5.3 프로젝트에 CCTV 화면을 붙이는 개발자를 위한 안내서입니다. 아래 명령과 설정은 적용 담당자가 이후에 사용할 예시입니다. 이 안내서를 작성하면서 운영 프로젝트를 수정하거나 새로 빌드·실행하지 않았습니다.

## 1. 한눈에 보기

`DTCctv`는 RTSP 비디오를 비동기로 받아 디코딩하고 UMG·머티리얼에 표시합니다. Windows `Win64`와 `Linux`를 대상으로 합니다. DTCore가 없어도 사용할 수 있고, 있으면 패키지의 이벤트 로그를 선택적으로 연동합니다. 화면 에셋은 운영 프로젝트가 만듭니다.

| 범위 | 내용 |
|---|---|
| 검증한 비디오 입력 | H.264 High, H.265 Main·Main10·4K, MJPEG 4:2:2. 형식·채널 수별 제한은 [코덱 검증](../README.md#코덱별-검증cctv-01g)과 [채널 수 표](../README.md#코덱별-최대-채널-수-cctv-01l) 참조 |
| 디코더 선택 | `Auto`: CUDA/NVDEC → Windows D3D11VA 또는 Linux VAAPI → 소프트웨어. 장치·드라이버·형식이 지원해야 하며 HW 슬롯 초과 시 소프트웨어 사용 |
| 화면 | `UDTCctvView` 또는 `UDTCctvStream::GetTexture()`로 연결. Linux Vulkan의 실제 화면 표시는 미검증 |
| 제공하지 않는 기능 | 오디오 재생, 탐색·되감기 API. 오디오는 SDP 트랙 정보만 조회 |

## 2. 설치

### 2.1 원본과 Git LFS 준비

1. Git LFS를 준비하고 DTCctv 원본 저장소에서 다음 명령을 사용합니다. 현재 작업 폴더의 바이너리를 운영 프로젝트로 가져오기 **전에** 실제 파일을 받습니다.

   ```powershell
   git lfs install
   git -C '<DTCctv 저장소 경로>' lfs pull
   git -C '<DTCctv 저장소 경로>' lfs ls-files
   ```

2. DLL·버전 접미사 `.so.*`가 `version https://git-lfs.github.com/spec/v1`로 시작하는 포인터 텍스트라면 준비가 끝나지 않은 상태입니다. 포인터는 실제 라이브러리가 아니므로 실행에 사용할 수 없습니다.
3. CCTV-01Y부터 Windows import `.lib`와 Linux `libdtcctv_*.so` 링커 스크립트는 제거했습니다. DTCctv는 스테이징된 DLL/SO의 정확한 전체 경로를 열고 함수 표로 호출합니다. 버전 접미사 `.so.62` 등은 실제 ELF 바이너리여야 합니다. 이전 파일을 덧붙이지 말고 현재 파일 구성을 사용합니다.

### 2.2 복사 설치를 기본으로 사용

1. 운영 프로젝트의 `Plugins/DTCctv/`에 다음 항목을 복사합니다.

   | 가져올 것 | 가져오지 않을 것 |
   |---|---|
   | `DTCctv.uplugin`, `Source/`, `Shaders/`, `README.md` | `TestHost/`, 플러그인 루트의 `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.git/` |
   | `Source/ThirdParty/DTCctvFFmpeg/` 전체: 헤더·라이브러리·`LICENSE.txt`·`README.md` 포함 | 원본 저장소의 `.gitattributes`를 운영 루트용 규칙으로 그대로 사용하지 않음 |
   | README의 문서 링크를 유지하려면 `docs/`도 함께 복사 | 시험 패키지·시험 보고서·시험 카메라 설정 |

   `Source/ThirdParty/.../Win64/bin/`의 DLL은 필수입니다. 제외하는 플러그인 루트 `Binaries/`와 혼동하지 마십시오. LGPL 라이선스·출처 문서는 배포 준비에도 유지합니다. 상세 내용은 [동봉 FFmpeg 설명](../Source/ThirdParty/DTCctvFFmpeg/README.md)을 따릅니다.

2. 기존 `.uproject`의 `Plugins` 배열에 다음 객체를 합칩니다. DTCore·DTDevice·PixelStreaming 등 기존 항목은 유지합니다.

   ```json
   { "Name": "DTCctv", "Enabled": true }
   ```

3. C++ 호출 모듈의 `.Build.cs`에 `DTCctv` 의존성을 추가합니다. 구현 파일에서만 쓰는 경우의 예시입니다.

   ```csharp
   PrivateDependencyModuleNames.Add("DTCctv");
   ```

   공개 헤더가 DTCctv 타입을 노출한다면 `PublicDependencyModuleNames`에 둡니다. FFmpeg 헤더나 `DTCctvFFmpeg`를 운영 코드에서 직접 참조하지 않습니다. 기존 프로젝트 의존성은 제거하지 않습니다. 적용 후에는 해당 운영 프로젝트에서 소스를 빌드해야 합니다.

**서브모듈 방식 판단:** 저장소 루트에 `DTCctv.uplugin`, `Source/`, `Shaders/`가 있으므로 `Plugins/DTCctv/`를 서브모듈 루트로 놓는 구조 자체는 가능합니다. 다만 이 저장소는 `TestHost/`와 시험 스크립트·문서도 함께 받습니다. 운영 프로젝트에서 이 배치의 빌드·패키징을 검증하지 않았으므로 기본 권장은 필요한 항목만 복사하는 방식입니다. TestHost가 있다는 사실만으로 서브모듈 설치가 반드시 실패한다고 판단하지 않습니다. 서브모듈을 선택하면 그 저장소 안에서 LFS 원본도 받아야 하며, 기존 DTCore·DTDevice의 gitlink를 변경하지 않아야 합니다.

### 2.3 운영 저장소의 LFS 규칙

읽기 전용으로 확인한 현재 작업 폴더는 다음과 같습니다. 다른 PC·브랜치에서는 다시 확인하십시오.

| 작업 폴더 | 실제 루트 프로젝트 파일 | `Plugins/` 구성 | 루트 `.gitattributes` |
|---|---|---|---|
| `ma0t10_dt` | `ma0t10_dt.uproject` | DTCore·DTDevice 모두 Git 서브모듈 | 없음 |
| `m7at10_dt` | `ma0t10_dt.uproject` | DTCore Git 서브모듈, DTDevice 폴더 없음 | 없음 |

두 폴더의 `.uproject`에는 PixelStreaming·DTCore가 켜져 있고 FFMPEGMedia 항목·폴더는 없습니다. 아래 공존 안내는 **FFMPEGMedia가 설치되어 있는 경우**에 적용합니다.

복사한 FFmpeg 바이너리를 운영 저장소에서 직접 커밋한다면 루트 `.gitattributes`에 복사 위치에 맞는 규칙을 추가합니다. 아래는 `Plugins/DTCctv/`에 설치했을 때의 예시입니다. 기존 규칙과 합치고, 다른 플러그인의 속성을 덮어쓰지 마십시오.

```gitattributes
Plugins/DTCctv/Source/ThirdParty/DTCctvFFmpeg/Win64/bin/*.dll filter=lfs diff=lfs merge=lfs -text
Plugins/DTCctv/Source/ThirdParty/DTCctvFFmpeg/Linux/lib/*.so.* filter=lfs diff=lfs merge=lfs -text
```

`git check-attr filter -- Plugins/DTCctv/Source/ThirdParty/DTCctvFFmpeg/Win64/bin/avcodec-62.dll`로 실제 적용을 확인합니다. 서브모듈 내부 파일은 부모 저장소가 직접 추적하는 파일과 구분합니다. 런타임 DLL·SONAME 파일의 이름과 원본 해시는 바꾸지 않습니다.

## 3. DTCore 로그 연동

빌드 시 `Source/DTCctv/DTCctv.Build.cs`가 다음 순서로 연동을 선택합니다.

1. `Target.ProjectFile`이 없거나 `.uproject`에서 DTCore를 명시적으로 `Enabled=false`로 설정했으면 `DTCCTV_WITH_DTCORE=0`입니다.
2. 프로젝트 `Plugins`, 엔진 `Plugins`, 엔진 `Plugins/Marketplace`, 프로젝트의 `AdditionalPluginDirectories`를 확인합니다. 각 루트 자체와 바로 아래 `DTCore`에서 **`DTCore.uplugin`과 `Source/DTCore/Public/Core/DxLogSubsystem.h`가 모두 있는지** 검사합니다. 임의 깊이의 모든 폴더를 재귀 탐색하지는 않습니다.
3. 발견하면 `DTCCTV_WITH_DTCORE=1`과 DTCore 모듈 의존성을 추가합니다. DTCctv의 `.uplugin`에서도 DTCore는 `Optional=true`입니다. 실행 시 DLL만 추가하는 방식으로 빌드 선택을 바꿀 수 없습니다.

| 실행 조건 | 로그 경로 |
|---|---|
| Editor 빌드·PIE | UE의 `LogDTCctv` |
| DTCore 없는 패키지, 서브시스템 준비 전·종료 중 | UE의 `LogDTCctv`로 폴백 |
| DTCore가 준비된 패키지 | 게임 스레드에서 `UDxLogSubsystem::WriteLog(..., false)`에 `[DTCctv]` 이벤트 전달 |
| 위 패키지에서 `bMirrorPackagedLogsToUE=True` | DTCore 파일과 UE 로그 양쪽에 이벤트 기록. 기본값 |
| 위 패키지에서 `bMirrorPackagedLogsToUE=False` | 준비된 DTCore 파일로 이벤트 전달. 모듈 초기화·폴백 등 UE 로그까지 모두 없애는 설정은 아님 |

DTCore 파일은 `DxLog_YYYY-MM-DD.log` 형식입니다. 현재 두 프로젝트의 DTCore 구현은 로그 디렉터리 결정 방식이 다릅니다.

| DTCore 복사본 | 디렉터리 결정 |
|---|---|
| `ma0t10_dt` | `UDTCoreSettings.LogDirectory` 사용. 빈 값은 `FPaths::LaunchDir()/Logs/CustomLogs`, 상대 경로는 LaunchDir 기준 |
| `m7at10_dt` | Windows는 `FPaths::LaunchDir()/Logs/CustomLogs`. Linux는 `FPaths::LaunchDir()/<실행 파일명에서 -Linux- 뒤를 제외한 이름>/Logs/CustomLogs`. 해당 구현은 LogDirectory 설정을 사용하지 않음 |

운영에서 연동 여부는 **패키지로 재생한 뒤 DTCore 파일의 `[DTCctv]` 연결·상태·스트림 정보 행**으로 확인합니다. DTCore 초기 로그의 `Log Directory:`도 확인하십시오. Editor에서 UE 로그만 보인다는 사실은 연동 실패의 근거가 아닙니다. 빌드 선택은 위 두 파일의 존재·명시적 비활성화·해당 타깃의 `DTCCTV_WITH_DTCORE` 정의와 함께 확인합니다. 이 PC의 운영 프로젝트에 DTCctv를 실제로 적용한 로그는 미검증입니다.

## 4. 화면 만들기

UMG에서 다음 한 경로로 연결합니다. 위젯 블루프린트는 운영 프로젝트에 둡니다.

| 항목 | 값 |
|---|---|
| 부모 클래스 | `UserWidget` |
| 팔레트 | `DT CCTV` → `DTCctvView` |
| 위젯 이름 | `CctvView`, `Is Variable` 켜기 |
| `bAutoPlay` | 런타임 주소를 넣는 이 예시에서는 끄기. 소스 기본값은 `true` |
| `Stretch` | `ScaleToFit`으로 비율 유지. `Fill`은 슬롯에 맞춰 늘림 |
| 배치 | 운영 UI의 Canvas·SizeBox 슬롯 크기와 앵커로 지정 |

1. `Event Construct`에서 런타임 설정으로 `Make DTCctvStreamConfig`를 구성합니다. `Url`, 선택적인 `SubUrl`, `Transport=Tcp`, `DecodeMode=Auto`를 넣습니다.
2. `CctvView.Config`를 설정하고 `Play`를 호출합니다.
3. `GetStream`의 유효성을 확인한 뒤 `OnStateChanged`에 운영 위젯의 이벤트를 바인딩합니다. 최초 표시는 `GetState`를 읽습니다. 재연결 문구·스피너·오류 배너는 운영 위젯이 만듭니다.
4. `Event Destruct`에서 `CctvView.Stop`을 호출합니다. `Remove from Parent`만으로 즉시 스트림이 닫힌다고 가정하지 않습니다.

`DTCctvView`는 표시 크기를 자동 보고합니다. `Reconnecting` 동안 마지막 영상이 남을 수 있으므로 상태 표시를 함께 제공합니다. 직접 스트림을 만드는 C++ 경로는 다음과 같습니다. 호출 객체가 유효한 GameInstance를 갖고 있고, 아래 코드를 게임 스레드에서 실행한다는 전제입니다.

```cpp
#include "DTCctvStream.h"
#include "DTCctvSubsystem.h"
#include "Engine/GameInstance.h"

// 호출 객체의 멤버: UPROPERTY(Transient) TObjectPtr<UDTCctvStream> Stream;
FDTCctvStreamConfig Config;
Config.Url = RuntimeCameraUrl;
Config.SubUrl = RuntimeSubStreamUrl;
UDTCctvSubsystem* Cctv = GetGameInstance()->GetSubsystem<UDTCctvSubsystem>();
Stream = Cctv->OpenStream(Config);
Stream->ReportViewSize(FVector2D(480.0, 270.0));
// Stream->GetTexture()를 운영 브러시·머티리얼에 연결합니다.
// 화면을 닫을 때 Cctv->CloseStream(Stream); Stream = nullptr; 를 호출합니다.
```

뷰로 열었다면 `Stop`, 서브시스템으로 열었다면 `CloseStream`을 사용합니다. 직접 소유한 `UDTCctvStream`에는 `Close`가 있습니다. 공개 함수는 게임 스레드에서 호출합니다.

## 5. 카메라 주소와 보안

1. 실제 주소·계정은 실행 시 운영 설정이나 보호된 입력에서 전달합니다. 위젯 기본값·블루프린트 에셋·소스·커밋 대상 ini에 인증 URL을 저장하지 않습니다. 예시는 `rtsp://<카메라 주소>/...` 형식이며 실제 경로는 카메라 설정에서 확인합니다.
2. 운영 코드에서 URL을 로그에 써야 한다면 `DTCctvStreamInfo.h`의 `DTCctvMaskUrlCredentials`를 사용합니다. 플러그인의 로그도 이 함수를 사용합니다. 가림은 저장 암호화나 모든 비밀 문자열의 자동 검출을 뜻하지 않으므로 원본 설정·비밀번호를 따로 출력하지 않습니다.
3. 작은 타일에는 실제로 해상도·비트레이트가 낮은 서브 주소를 `SubUrl`로 넣습니다. 메인 주소를 그대로 반복해 넣으면 디코딩 부하가 줄지 않습니다.
4. 자동 전환은 표시 폭이 기본 `960px` 미만인 상태가 `2초` 유지될 때 선택합니다. 전환 세션의 첫 프레임을 받은 뒤 교체합니다. 직접 `SetUseSubStream`을 호출하면 그 Open 수명 동안 수동 선택이 우선하고, 다시 `Open`하면 자동 선택이 초기화됩니다.

## 6. 프로젝트 설정 권장값

`UDTCctvSettings`는 `config=Game` 설정입니다. 처음에는 아래 기본값을 유지하고 실제 운영 부하를 확인한 뒤 필요한 항목만 조정합니다.

| 이름 | 기본값 | 바꾸는 경우 |
|---|---:|---|
| `MaxConcurrentConnects` | `8` | 카메라·서버의 동시 연결 제한에 맞춰 연결 시도 수를 조정 |
| `MaxQueuedPackets` | `120` | 채널당 패킷 큐. 초과 시 다음 키프레임까지 버리므로 수치를 늘려 과부하를 숨기지 않음 |
| `SubStreamMaxWidth` | `960` | 운영 화면의 작은 타일 폭에 맞춰 서브 선택 경계 조정 |
| `MaxHardwareDecodeStreams` | `4` | 프로세스 내 HW 디코딩 슬롯 상한. 실제 CPU·NVDEC·FPS를 확인하며 조정 |
| `SoftwareDecodeThreads` | `2` | SW 디코딩의 채널별 스레드. CPU 경합이 확인된 경우에만 별도 비교 |
| `bMirrorPackagedLogsToUE` | `true` | DTCore 패키지의 이벤트를 UE에도 남길지 선택. 새 스트림부터 반영 |
| `bIgnoreWindowsTimerThrottling` | `false` | Windows 11의 가려진 창에서 타이머 제한이 문제일 때 명시적으로 선택. 프로세스 전체에 적용되며 전력 소모 증가 가능. 재시작 후 적용 |

`DefaultGame.ini`의 기본값 예시입니다. 이 문서 작업에서 실제 ini나 기본값은 바꾸지 않았습니다.

```ini
[/Script/DTCctv.DTCctvSettings]
MaxConcurrentConnects=8
MaxQueuedPackets=120
SubStreamMaxWidth=960
MaxHardwareDecodeStreams=4
SoftwareDecodeThreads=2
bMirrorPackagedLogsToUE=True
bIgnoreWindowsTimerThrottling=False
```

타이머 옵션은 `-DTCctvIgnoreTimerThrottling=1`로 켜고 `=0`으로 끕니다. 유효한 명령줄 값이 설정을 우선하며 다른 값은 설정으로 폴백합니다. Windows 11에서만 적용하고 Windows 10·Linux에서는 적용하지 않습니다. 시작 로그의 `WindowsTimerThrottling enabled=... source=... result=...`를 확인합니다. `result=applied`가 적용 성공이며 `query_failed`·`apply_failed`는 성공이 아닙니다. PC 잠금·최소화·모든 전원 정책에서 같은 성능을 보장하는 옵션은 아닙니다.

카메라별 `FDTCctvStreamConfig`의 기본값도 구분합니다.

| 이름 | 기본값 |
|---|---|
| `Url`, `SubUrl` | 빈 문자열. 메인 주소 필수, 서브 주소 선택 |
| `Transport`, `DecodeMode` | `Tcp`, `Auto` |
| `ConnectTimeoutSeconds`, `ReadTimeoutSeconds` | 각각 `5초` |
| `MinReconnectDelaySeconds`, `MaxReconnectDelaySeconds` | `0.5초`, `2초` |

재연결 대기에는 지터가 추가됩니다. 카메라가 실패 요청을 제한한다면 재연결 대기와 동시 연결 수를 조정합니다. 정상 연결을 주기적으로 끊는 설정은 제공하지 않습니다.

## 7. 채널 수 계획

아래는 [README의 코덱별 채널 수 표](../README.md#코덱별-최대-채널-수-cctv-01l)를 적용 계획용으로 줄인 값입니다. `32 이상`은 상한을 찾았다는 뜻이 아니라 32까지 확인했다는 뜻입니다.

| 입력 | Windows HW4+SW | Linux SW | Linux 자동 |
|---|---:|---:|---:|
| H.264 High 1080p30 B2 / H.265 Main 1080p30 | 32 이상 | 32 이상 | 32 이상 |
| H.265 Main 4K30 | 16 | 16 | 16 |
| H.264 1080p60 | 20 | 32 이상 | 32 이상 |
| H.265 Main 1080p60 | 18 | 30 | 30 |
| H.265 Main10 1080p30 | 32 이상 | 28 | 28 |
| MJPEG 4:2:2 1080p30 | 32 이상 | 10 | 12 |

위 값은 로컬 반복 파일의 검증값입니다. Windows는 오프스크린, Linux는 화면 없는 디코딩만 확인한 결과이며 운영 보증값이 아닙니다.

기준 PC는 Ryzen 7 7800X3D·RTX4070Ti이며 Linux VM 메모리는 약 3.82GiB였습니다. 상세 조건과 판정 기준은 링크한 표를 참조합니다. MJPEG의 Linux 자동 경로도 이 PC에서는 실제 SW 디코딩이었습니다. 화면 크기·코덱·해상도·FPS와 동시 인코딩 부하가 달라지면 운영 서버에서 다시 확인합니다.

작은 타일은 서브 스트림으로 줄이고 채널을 단계적으로 늘립니다. 점검 UI의 상한은 16개이므로, 나눠 점검한 결과를 32채널 동시 부하의 합격으로 간주하지 않습니다.

## 8. Pixel Streaming 서버 실행 권장

운영 프로젝트의 기존 PixelStreaming 활성화와 시그널링 설정은 유지합니다. 이 안내서의 실행 예시는 표시·타이머 선택만 설명하며, 실제 Pixel Streaming 송출·인코딩 동시 부하는 미검증입니다.

| 운용 조건 | 선택 |
|---|---|
| Windows에서 사람이 영상을 확인 | 일반 창을 포그라운드로 유지하고 기본 타이머 옵션 꺼짐으로 확인 |
| 무인 GPU 서버 | 운영 패키지의 `-RenderOffScreen` 동작을 먼저 검증한 뒤 사용. 화면 없는 연결 점검용 `-nullrhi`와 구분 |
| Windows 11에서 다른 창 뒤의 일반 창이 필요 | 기본 꺼짐을 유지하며 시작하고, FPS 저하가 확인되면 타이머 옵션 켜짐을 명시적으로 선택·재검증 |
| Linux 서버 | GPU·드라이버·Vulkan 화면 표시를 별도 검증. 현재 nullrhi 디코딩 결과를 화면·Pixel Streaming 합격으로 확대하지 않음 |

```powershell
& '<운영 패키지 실행 파일>' -RenderOffScreen -ResX=1920 -ResY=1080
# 다른 창 뒤의 일반 창이 필요한 경우의 선택 예시
& '<운영 패키지 실행 파일>' -Windowed -ResX=1920 -ResY=1080 -DTCctvIgnoreTimerThrottling=1
```

UE 5.3 DX12의 오프스크린 시작에서 플러그인 비활성 대조에도 viewport ensure가 관측됐으므로 모든 패키지에서 성공한다고 가정하지 않습니다. 화면 점검 도구의 기본 경로는 일반 창입니다. 창 조건별 근거는 [창 상태 진단](WINDOW_STATE_DIAGNOSTICS.md)과 [README의 타이머 옵션](../README.md#창-상태와-백그라운드-fps-cctv-01w)을 참조합니다.

## 9. 배포 전 점검

### 9.1 TestHost 연결 점검 도구

점검 도구는 운영 프로젝트에 넣는 플러그인 기능이 아니라 별도 **TestHost 패키지**입니다. CCTV-01Z 정리에서 이전 임시 패키지를 삭제했으므로 보존된 시험 패키지를 별도로 준비하거나, 이후 승인된 작업에서 TestHost를 다시 패키징해야 합니다. 이 문서 작업에서는 만들지 않습니다.

1. Windows에서는 다음과 같이 일반 창으로 실행합니다. 인자 없이 실행해도 점검 화면이 열립니다.

   ```powershell
   & '<패키지>\Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe' -Windowed -ResX=1280 -ResY=900
   ```

2. 메인·서브 주소와 사용자 이름·비밀번호를 별도 필드로 입력하고 **추가**합니다. 필요 없는 행은 **삭제**합니다. TCP/UDP와 점검 시간을 선택하고 **시작**합니다. 기본은 60초이며 초기 10초 이후 구간이 필요합니다. 계정이 보이는 주소를 콘솔 명령줄에 직접 넣지 않습니다.
3. 영상·상태·FPS·오류를 확인합니다. 창 크기와 1열/2열/4열을 바꿔 서브 전환도 확인합니다. 완료되면 보고서 경로를 확인합니다. **중지**한 점검을 완료된 PASS로 보지 않습니다.
4. **주소 저장(계정 제외)**는 사용자가 선택할 때만 `Saved/CctvCheck/addresses.json`에 저장합니다. 인증 정보는 제거·가림 처리되므로 다시 사용할 때 인증을 입력합니다. 원본 입력 파일과 화면 캡처의 접근 권한도 관리합니다.

파일 입력은 다음 JSON 구조입니다. 예시는 비밀값을 넣지 않았습니다. 실제 인증을 넣은 JSON은 Git 밖의 접근 제한 경로에 두고 점검 후 관리 정책에 따라 처리합니다.

```json
{
  "schema_version": 1,
  "channels": [
    {
      "url": "rtsp://<카메라 주소>/...",
      "sub_url": "",
      "username": "",
      "password": "",
      "transport": "tcp",
      "software_only": false
    }
  ]
}
```

```powershell
& '<패키지>\Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe' -Windowed '-CctvCheckList=<보호된 입력 JSON 경로>' -CctvCheckSeconds=60
```

Linux의 화면 없는 연결·디코딩 점검 예시입니다. Saved 경로가 쓰기 가능해야 합니다.

```bash
'<패키지>/Linux/DTCctvTestHost.sh' -nullrhi -unattended '-CctvCheckList=<보호된 입력 JSON 경로>' -CctvCheckSeconds=60
native_code=$?
python3 TestHost/Scripts/cctv_check_result.py '<실제 보고서 경로>/report.json' --native-exit-code "$native_code"
```

`TestHost/Scripts/`는 원본 저장소에서 별도로 사용하는 도구입니다. 운영 플러그인 복사본에는 포함하지 않았습니다. Python3가 별도로 없는 RHEL 8 환경에서는 기존 `/usr/libexec/platform-python`으로 검증기를 실행할 수 있습니다.

| 판정 | 읽는 방법 |
|---|---|
| PASS | 초기 10초 이후 재생 유지·재연결/패킷 손실/디코딩 오류 0, 표시가 원본의 97% 이상이며 경고 사유 없음 |
| WARN | 재생은 됐지만 SW 디코딩·표시 FPS 미달·프레임 대체·첫 재생 5초 초과·원본 FPS 미확정 등의 사유가 있음. 사유별로 판단 |
| FAIL | 연결·인증·코덱 문제로 재생하지 못함, 점검 중 재생 중단 또는 유효 판정 구간 부족 등 |

`-nullrhi`는 표시·색·스크린샷을 확인하지 않아 WARN과 `screenshot_status=ENV_BLOCKED`를 남깁니다. 이를 화면 검증 PASS로 바꾸지 않습니다. 실제 보고서는 프로젝트의 `Saved/CctvCheck/<시각-PID-ID>/`에 `report.md`, `report.json`, `channel-NN.csv`, 표시 가능한 경우 `screenshot.png`로 저장됩니다. 화면·UE 로그의 실제 절대 경로를 사용하십시오.

결과 검증기의 종료 코드는 PASS `0` / WARN `2` / FAIL `1`입니다. Windows도 앱 종료 코드만으로 판정하지 말고 `cctv_check_result.py`와 JSON을 함께 확인합니다. 표시 FPS·frame age는 렌더 명령 등록 기준이며 실제 모니터 표시·네트워크 왕복 지연을 뜻하지 않습니다.

### 9.2 정보 로그와 배포 파일

1. `연결정보`에서 전송·코덱/프로파일/레벨·해상도·픽셀 형식·비트 깊이·선언 FPS·선택 디코더를 확인합니다. `안정화정보`는 재생 10초 이후 실측 FPS·비디오 payload 비트레이트·GOP·B 사용을 남깁니다. `GetStreamInfo()`로도 조회할 수 있습니다. 선언 FPS와 실측 FPS를 구분합니다.
2. Windows 패키지의 게임 실행 바이너리 옆에 다음 **5개 DLL**이 있는지 확인합니다. 설치 원본은 `Plugins/DTCctv/Source/ThirdParty/DTCctvFFmpeg/Win64/bin/`입니다.

   ```text
   avcodec-62.dll  avformat-62.dll  avutil-60.dll  swscale-9.dll  swresample-6.dll
   ```

3. Linux 패키지의 게임 실행 바이너리 옆에 다음 **5개 SONAME 파일**이 있어야 합니다. 설치 원본은 `Plugins/DTCctv/Source/ThirdParty/DTCctvFFmpeg/Linux/lib/`입니다. 현재 구성에는 링커 스크립트가 없습니다. 파일 존재뿐 아니라 원본과의 해시·실제 로드·디코딩을 확인합니다.

   ```text
   libavcodec.so.62  libavformat.so.62  libavutil.so.60  libswscale.so.9  libswresample.so.6
   ```

4. Linux 서버의 추가 점검에는 원본 저장소의 `TestHost/Scripts/linux_server_check.sh`와 동명의 `.py` 도우미를 함께 사용합니다. 이 도구는 16채널 하네스이며 앞의 60초 UI 점검과 판정 기준이 다릅니다. 아래는 H.265 30fps 예시입니다. 실제 카메라의 코덱·FPS에 맞춰 지정합니다.

   ```bash
   bash TestHost/Scripts/linux_server_check.sh '<패키지>/Linux' 'rtsp://<카메라 주소>/...' --decode-only --codec hevc --fps 30
   # 소프트웨어 디코딩만 확인하려면 --decode-only 대신 --software-only 사용
   ```

   `--decode-only`는 자동 디코더 선택, `--software-only`는 소프트웨어 디코더와 nullrhi입니다. 옵션 없이 실행하면 Vulkan GPU 경로를 시도하지만 이 PC에서 해당 화면 검증은 완료하지 못했습니다. 이 스크립트의 URL 인자는 프로세스 목록·셸 이력에 남을 수 있으므로 인증 정보가 든 주소에는 위의 보호된 JSON 입력 점검을 우선 사용합니다.

## 10. 기존 FFMPEGMedia 플러그인과 함께 설치할 때

현재 두 운영 폴더에는 기존 플러그인이 없습니다. 아래는 [UE-DT-CCTV-Plugin-FFMPEG](https://github.com/YonghoKwon/UE-DT-CCTV-Plugin-FFMPEG)가 **설치되어 있는 경우**의 주의점입니다. 운영 설치본의 이름만 같다는 이유로 시험본과 동일하다고 판단하지 않습니다.

| 조건 | 확인된 결과와 적용 주의 |
|---|---|
| Windows·Linux, 기존 플러그인을 켜 두고 사용하지 않음 | CCTV-01Y에서 DTCctv 16채널 통과. Windows 1080p60 decode/display 59.9790/59.8823fps, Linux 1080p30 SW 30.0224·자동 30.0105fps |
| Windows의 구 `avdevice-58.dll` 오류 126 | DTCctv 없이도 같은 오류 재현. 기존 제공본·의존 환경 문제로 구분하며 신 FFmpeg DLL을 덮어써 해결하지 않음 |
| 기존 플레이어만 4채널, DTCctv 설치됨 | Windows 유효124.936초·채널당3748~3749샘플, Linux TCP124.937초·각3748샘플. 시각 진행·Playing·오류0·종료0. Linux 초기 UDP 시험은 프레임0으로 실패했고 원자료 보존 |
| 두 플러그인 동시 영상 재생 | CCTV-01Y의 새 패키지에서 기존4+DTCctv12 통과. Windows 유효124.2초, DTCctv decode/display29.9980/29.9987fps·기존 각3,726샘플. Linux TCP 유효123.9초, DTCctv30fps·기존 각3,717샘플. 크래시·교착·함수 섞임0 |
| Editor PIE | 두 플러그인 활성 Editor 빌드와 실제 GUI Play/Stop에서 DTCctv4+기존1 영상·프레임 진행, 정상 종료 확인 |
| `feature/dt-project-sync`의 `ea78883` | 철자 오타 수정 후에도 빌드 실패. 공존 시험은 main `819e004` 사용 |

**함께 설치·동시 재생 가능**은 위 main `819e004`와 로컬 합성 RTSP 입력의 검증 범위입니다. DTCctv의 52개 함수는 전체 경로 실행 중 로드로 분리했고, Windows 실제 IAT·함수 주소와 Linux LD_DEBUG/maps에서 구 호출은58계열·신 호출은62계열로 확인했습니다. 신 라이브러리 내부 결합도 신 계열이며 `RTLD_DEEPBIND`는 필요하지 않았습니다. 기존 플러그인의 소스·빌드 규칙·바이너리·descriptor는 고치지 않았습니다. 상세 범위와 이전 V·I·R15 결과는 [공존 검증 기록](FFMPEG_MEDIA_COEXISTENCE.md)에 보존합니다.

**01V의 ABI 공존 회피를 위한 `PlatformDenyList`는 필요 없습니다.** 기존 FFMPEGMedia를 사용할 경우 활성 참조는 아래처럼 유지할 수 있습니다. 운영 프로젝트의 전체 빌드·패키징을 대신 검증한 것은 아닙니다.

```json
{ "Name": "FFMPEGMedia", "Enabled": true }
```

이 PC의 기존 Linux 플레이어는 기본 UDP에서 프레임이 진행하지 않았습니다. 재시험은 기존 설정 클래스가 제공하는 RTSP TCP를 시험 프로세스 명령줄에만 적용했으며 파일·기본값은 바꾸지 않았습니다. 운영에서는 해당 카메라의 전송 설정·환경을 확인하십시오. Windows의 구 avdevice 오류126은 기존 단독 상태에서도 관측된 별도 제한입니다.

CCTV-01Y 전체는 Y6 회귀 때문에 FAIL입니다. 장애 복구6.226초(기준5초), Close/Open20회 후반 GPU 기울기 +0.00004735MiB/회(비증가 기준)에 미달했습니다. 나머지 공존·코덱 회귀 통과와 구분하고, 실제 서버의 장시간 수명·화면·Pixel Streaming 부하를 별도로 확인합니다.

나중에 기존 플러그인을 제거할 때는 **운영 코드·미디어 에셋의 참조 제거 → `.uproject`의 FFMPEGMedia 항목 제거 → 플러그인 폴더 제거 → Windows·Linux 재빌드·패키징·점검** 순서로 진행합니다. 이 안내서는 운영 프로젝트의 실제 제거를 수행하지 않았습니다.

## 11. 문제 해결

| 상태 | 의미·운영 UI 처리 |
|---|---|
| `Idle` | 열리지 않았거나 닫힌 상태. 필요하면 운영 UI에서 다시 재생 요청 |
| `Connecting` | 최초 연결·분석·첫 디코딩을 진행 중 |
| `Playing` | 재생 상태. `GetStats`와 표시를 함께 확인 |
| `Reconnecting` | 실제 장애 후 복구 중. 마지막 프레임 위에 상태를 표시하고 `LastError`·`LastErrorCode` 확인 |

| 증상 | 확인할 것 |
|---|---|
| 401 인증 실패 / 403 접근 거부 | 계정·권한·시험 주소 이용 조건·접근 정책. 인증을 로그에 출력하거나 접근 제한을 우회하지 않음 |
| UDP에서 연결·영상 불안정 | 카메라·방화벽의 UDP 경로를 확인하고 `Transport=Tcp`로 별도 점검. 원격지 TCP 지연 성능은 별도 검증 |
| 코덱 미지원·HW 초기화 실패 | `연결정보`, `GetStreamInfo().DecoderName`·`DecoderReason`, `GetStats().LastError` 확인. 사용 가능한 SW 폴백과 형식 지원을 구분 |
| 채널 증가 후 FPS 저하 | 채널별 DecodeFps·DisplayFps, PacketsDropped·FramesSuperseded, CPU·NVDEC와 서브 주소 확인. 기준을 낮춰 PASS로 바꾸지 않음 |
| 다른 창 뒤에서 표시 FPS 저하 | 포그라운드 비교 후 Windows 11 타이머 옵션을 선택·재검증. [창 상태 진단](WINDOW_STATE_DIAGNOSTICS.md) 참조 |
| DTCore 로그가 없음 | Editor인지, 빌드 감지 경로·비활성화·서브시스템 준비 상태·해당 DTCore의 실제 디렉터리 확인 |
| FFmpeg 로드 실패 | LFS 포인터, 누락된 SONAME/DLL, `FFmpegLoader`의 전체 경로·누락 함수 오류 확인. 세션을 만들지 않고 Idle·LastError로 끝남 |
| 두 플레이어를 열자 구 avformat에서 충돌 | 01Y 함수 표가 포함된 빌드인지, 신 계열 직접 import가 남았는지 확인. 기존 플러그인·구 라이브러리를 신 바이너리로 덮어쓰지 않음 |

정상 연결의 정기 재시작을 장애 대응책으로 넣지 않습니다. 화면을 닫을 때는 `Stop`/`CloseStream`으로 의도적으로 닫습니다.

## 12. 운영에서 아직 확인해야 하는 것

1. Linux Vulkan에서 실제 화면·비율·색·스크린샷 검증.
2. Pixel Streaming의 실제 송출·인코딩과 CCTV 재생 동시 부하.
3. 실제 카메라·원격지 TCP의 지연·지터·손실·접근 정책과 카메라별 동시 연결 제한. 로컬 반복 파일의 채널 수를 그대로 보증값으로 사용하지 않음.
4. 실제 운영 화면 수명에서 Close/Open 장시간 자원 관찰. 기존 자원 변화의 사용자 수용은 운영 장시간 관찰 완료를 뜻하지 않음.
5. 운영 프로젝트별 설치·DTCore 패키지 로그, Windows 10 실제 타이머 옵션 동작, 해당 운영 FFMPEGMedia 설치본의 공존 확인. 로컬 main `819e004`의 동시 재생 통과를 다른 버전·입력으로 확대하지 않습니다.

## 13. 적용 체크리스트

- [ ] 적용할 운영 브랜치와 실제 `.uproject` 파일명·기존 플러그인 목록을 확인했다.
- [ ] Git LFS 원본을 받고 DLL·버전 `.so`가 포인터가 아닌지 확인했다.
- [ ] 필수 Source·Shaders·descriptor·라이선스만 계획한 경로에 복사하고 TestHost·시험 산출물을 제외했다.
- [ ] 복사 위치에 맞는 LFS 규칙과 런타임 DLL·SONAME 이름·해시를 확인했다.
- [ ] 기존 DTCore·DTDevice gitlink와 PixelStreaming 설정을 유지했다.
- [ ] DTCctv 활성화와 호출 모듈 의존성을 적용하고 해당 프로젝트의 빌드·패키징을 확인했다.
- [ ] 실제 인증 정보를 에셋·소스·Git 추적 ini·명령줄·로그에 남기지 않았다.
- [ ] UMG Config→Play→상태 바인딩→Stop의 수명을 연결했다.
- [ ] 기본값에서 시작해 메인·서브 주소와 채널 수를 계획했다.
- [ ] 창 상태·오프스크린·타이머 옵션 선택과 실제 적용 로그를 확인했다.
- [ ] TestHost로 실제 주소를 점검하고 PASS·WARN·FAIL의 사유와 보고서를 검토했다.
- [ ] 패키지의 FFmpeg 파일·실제 디코더·DTCore 로그 경로를 확인했다.
- [ ] FFMPEGMedia 설치본 버전·전송 설정·기존 참조와 실제 동시 재생을 확인했다.
- [ ] 실제 서버의 표시·Pixel Streaming 동시 부하·원격지·장시간 Close/Open을 검증했다.
