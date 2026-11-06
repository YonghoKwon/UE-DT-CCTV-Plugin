# DTCctv

UE 5.3 프로젝트에서 RTSP CCTV를 표시하는 Windows·Linux용 C++ 플러그인입니다. DTCore가 없는 프로젝트에서도 동작하며, DTCore가 있는 패키지에서는 선택적으로 로그를 연동합니다. 플러그인 안에 블루프린트·맵·위젯 에셋을 넣지 않습니다. 오디오는 재생하지 않고 SDP 트랙 정보만 조회합니다.

CCTV-01F의 최종 C++는 `95cd411`입니다. Editor·전체 단위 15개·새 Win64/Linux 패키징을 완료했고, 소비자 정지 시 역압이 남는 결함을 수정했습니다. GT 3/5/10초와 렌더 정체, 지연·버스트 회귀, Windows 패키지·디코딩/로드 공존·UI·장애·소크는 통과했습니다. 100회 자원 비증가 측정의 원시 판정은 FAIL이며, GPU 약+0.12MiB·핸들 최대1692→1696은 2026-11-06 사용자 결정으로 **수용됨, 운영에서 장시간 관찰 권장**입니다. 해당 항목의 추가 수정은 수행하지 않습니다. Linux Vulkan 렌더링은 ENV_BLOCKED입니다.

## 코덱별 검증(CCTV-01G)

H264 High B2·GOP 12초, H265 Main 30/60fps·GOP 12초·Main10·4K 및 메인/서브 혼합을 Windows·RHEL 8에서 측정했습니다. 정상 SW 형식 폴백의 불필요한 재접속을 없앤 최종 C++는 `9c98d85`입니다. MJPEG 4:2:2는 재접속 문제를 고쳤지만 Windows p95 73ms, Linux 16채널 FPS 미달과 실제 CUDA 초기화 불가가 남아 전체 코덱 판정은 **FAIL**입니다.

| 조건 | Windows decode/display·p95 | Linux SW16 | Linux 최대HW4 |
|---|---|---|---|
| C1 H264 High 1080p30 B2 GOP2초 | PASS 29.989/29.981, p95 20ms | PASS 30.011 / 최저 30.000 | PASS 30.006 / cuda4,software12 |
| C2 H264 High 1080p30 GOP12초 | PASS 29.977/29.978, p95 19ms | PASS 30.011 / 최저 29.999 | PASS 30.000 / software12,cuda4 |
| C3 H265 Main 1080p30 B2 GOP2초 | PASS 29.969/29.978, p95 21ms | PASS 29.978 / 최저 29.996 | PASS 30.011 / software12,cuda4 |
| C4 H265 Main 1080p60 GOP2초 | PASS 59.989/59.589, p95 36ms | PASS 59.985 / 최저 59.990 | PASS 59.985 / cuda4,software12 |
| C5 H265 Main 1080p30 GOP12초 | PASS 29.976/29.975, p95 21ms | PASS 30.009 / 최저 29.988 | PASS 30.011 / cuda4,software12 |
| C6 H265 Main10 1080p30 | PASS 30.025/30.022, p95 22ms | PASS 29.978 / 최저 29.993 | PASS 29.979 / cuda4,software12 |
| C7 H265 Main 4K30 | PASS 30.014/30.019, p95 31ms | FAIL 29.599 / 최저 27.829 | PASS 30.001 / software12,cuda4 |
| C8 MJPEG 1080p30 4:2:2 | FAIL 29.724/29.793, p95 73ms | FAIL 27.756 / 최저 22.136 | FAIL 27.119 / software16 |
| C9 H265 4K 메인/H264 640 서브 | PASS 30.032/30.028, p95 43ms | PASS 30.060 / 최저 29.997 | PASS 30.011 / cuda4,software12 |

GOP 2초·1080p30 H264/H265와 작은 뷰의 H264 640 서브를 본 측정 기준의 시작 설정으로 권장합니다. 기본 HW4는 H265에서도 유지했으며 `프로젝트 설정 → Game → DT CCTV → MaxHardwareDecodeStreams`로 조정합니다. 긴 GOP의 첫 화면 대기·스레드 설정·코드 버전·HW 미지원과 운영 점검 명령은 [코덱 검증 상세](docs/CODEC_VALIDATION.md)를 참조하십시오. Linux 렌더링·실제 카메라·실제 동시 인코딩 부하는 미검증입니다. 입력 규격을 낮춘 별도 조건으로 기존 FAIL을 바꾸지 않았습니다.

## 현장 점검 방법 (CCTV-01J)

현장 점검 화면은 운영 프로젝트에 도구를 설치하지 않고 **TestHost 패키지**에서 실행합니다. Windows 실행 파일은 `<패키지>/Windows/DTCctvTestHost/Binaries/Win64/DTCctvTestHost.exe`입니다. 별도 명령줄 인자 없이 실행하면 점검 화면이 열립니다. 현재 PC에서 만든 패키지의 실제 위치와 검증 자료는 `Saved/Reports/CCTV-01J/result.json`에 기록합니다.

1. 메인 RTSP 주소와 필요한 서브 주소를 넣습니다. 사용자 이름·비밀번호를 별도 필드로 넣거나 URL의 userinfo 형식을 사용할 수 있습니다. 입력 필드는 가림 처리되고, **추가** 후 목록에는 계정이 가려진 주소가 보입니다.
2. **추가**·**삭제**로 주소를 1~16개 준비합니다. 전송 방식은 TCP/UDP로 선택할 수 있습니다. **점검 초**의 기본값은60이며 초기10초보다 큰 시간을 지정해야 합니다.
3. **시작**을 누르면 격자 영상에 상태·코덱/프로파일·해상도·원본/디코드/표시 FPS·HW/SW·재연결·손실·오류·대체·frame age·최근 오류가 표시됩니다. 창 크기와 **1열/2열/4열**을 바꿔 크기와 서브 전환도 확인할 수 있습니다.
4. 시간이 지나면 채널별 결과 표와 보고서 경로가 표시됩니다. **중지**로 조기 종료한 점검은 완료 구간 부족 또는 중단 사유로 FAIL입니다.

| 판정 | 의미 |
|---|---|
| PASS | 초기10초 이후 재생 유지, 재연결·패킷 손실·디코딩 오류0, 표시가 원본97% 이상이고 경고 없음 |
| WARN | 재생은 되지만 FPS·지속 재생 조건 미달, SW 디코딩, 표시 전 대체, 첫 재생5초 초과, 원본 FPS 미확정 또는 화면 없는 표시 미검증 |
| FAIL | 연결·인증·코덱 오류로 재생을 못 했거나 점검 중 재생이 중단됨 |

보고서는 실행 프로젝트의 `Saved/CctvCheck/<시각-PID-ID>/`에 `report.md`, `report.json`, `channel-NN.csv`, `screenshot.png`로 저장합니다. 화면과 UE 로그에 실제 절대 경로가 표시됩니다. 표시 FPS는 렌더 명령 등록 횟수이며 frame age는 디코더 반환~등록 구간입니다. GPU 완료·모니터 표시·네트워크 왕복 지연은 별도 범위입니다.

**주소 저장(계정 제외)**는 사용자가 누를 때만 `Saved/CctvCheck/addresses.json`을 만듭니다. userinfo를 제거하고 password/token 등 민감 query 값을 가립니다. 비밀번호를 보관하는 옵션은 없습니다. 재사용 시 필요한 인증을 다시 입력합니다. `Saved/`는 Git 추적에서 제외됩니다.

### 파일 기반 자동 점검과 Linux

목록 JSON은 다음 형식입니다. 실제 자격증명을 넣는 입력 파일은 운영자가 별도로 보호하며, 보고서에는 가려진 주소만 남습니다.

```json
{
  "schema_version": 1,
  "channels": [
    {
      "url": "rtsp://camera-host/stream",
      "sub_url": "",
      "username": "",
      "password": "",
      "transport": "tcp",
      "software_only": false
    }
  ]
}
```

```bash
<패키지>/Linux/DTCctvTestHost.sh -nullrhi -unattended -CctvCheckList=/opt/cctv/check.json -CctvCheckSeconds=60
python3 TestHost/Scripts/cctv_check_result.py <실제-보고서-경로>/report.json
```

RHEL8에 python3 명령이 없으면 기존 `/usr/libexec/platform-python`으로 결과 검증기를 실행할 수 있습니다. `-nullrhi`는 연결·디코딩·원인과 CSV/JSON/MD를 점검하며, 표시·색·스크린샷 미검증을 WARN과 `screenshot_status=ENV_BLOCKED`로 보고합니다. 패키지의 Saved 디렉터리는 쓰기 가능해야 합니다. 기존 `linux_server_check.sh --software-only`·`--decode-only --codec hevc --fps 30`은 기존 디코딩 수용 기준으로 함께 사용할 수 있습니다.

자동화 결과 검증기 `cctv_check_result.py`는 JSON의 채널·전체 판정 일관성을 확인하고 **PASS0 / WARN2 / FAIL1**을 반환합니다. `--native-exit-code <실행코드>`를 함께 주면 비정상 프로세스 종료도 거부합니다. UE5.3 Windows의 비강제 종료에서 네이티브 코드0이 관측되므로 Windows 자동화도 이 JSON 결과 검증기를 사용합니다.

### 스트림 정보 API와 DTCore 로그

`UDTCctvStream::GetStreamInfo()`의 BlueprintReadOnly `FDTCctvStreamInfo`에서 계정 가린 주소·전송·SDP 도구/세션·트랙·선택 인덱스·코덱/프로파일/레벨·픽셀/비트/크로마·선언 FPS/B·디코더/이유를 조회합니다. 재생10초 이후 실측 디코드 FPS·압축 비디오 payload 평균 bitrate·관측 GOP·B 사용을 한 번 기록합니다. 긴 GOP의 두 번째 키프레임이 늦으면 관측 완료 시 GOP를 추가 기록합니다. 전송 후 프레임 형식은 HW 다운로드 직후·swscale 전의 CPU 형식입니다. 오디오는 SDP에서 열거하며 수신·재생 대상은 비디오입니다.

DTCctv를 운영 프로젝트에 설치할 때 DTCore가 있으면 소스를 해당 프로젝트에서 다시 빌드합니다. Build.cs가 프로젝트의 `Plugins/DTCore` 또는 AdditionalPluginDirectories의 DTCore와 로그 헤더를 확인해 `DTCCTV_WITH_DTCORE=1`·모듈 의존성을 선택하고, 없거나 명시적으로 비활성화됐으면0으로 빌드합니다. `.uplugin`의 DTCore 참조는 Optional입니다. TestHost에는 DTCore를 넣지 않습니다.

Editor 빌드(PIE 포함)는 `LogDTCctv`에 기록합니다. DTCore가 있는 패키지는 연결·재연결·상태·스트림 정보·오류를 게임 스레드에서 `UDxLogSubsystem::WriteLog(..., false)`로 전달합니다. 날짜별 `DxLog_YYYY-MM-DD.log`의 `[DTCctv]` 행과 UE 로그를 확인합니다. DTCore/서브시스템이 준비되지 않았거나 종료 중이면 UE 로그로 기록합니다. GameInstance와 서브시스템은 약한 참조로 다룹니다.

```ini
[/Script/DTCctv.DTCctvSettings]
bMirrorPackagedLogsToUE=True
```

기본값 True는 DTCore 파일과 UE 양쪽에 남깁니다. False는 준비된 DTCore 패키지의 실행 중 이벤트를 파일로만 보냅니다. 새 스트림부터 반영되며 DTCore 파일 위치는 DTCore의 LogDirectory 설정을 따릅니다. 운영 프로젝트의 실제 적용·현장 장시간 수용은 사용자가 진행합니다.

### 화면 유무에 따른 실행 방법

Windows에서 영상을 확인할 때는 일반 창으로 실행합니다. 자동 목록 입력도 같은 화면에서 사용할 수 있습니다.

```powershell
& '<패키지>\Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe' -Windowed -ResX=1280 -ResY=900 '-CctvCheckList=C:\cctv\check.json' -CctvCheckSeconds=60
```

UE5.3의 DX12 `-RenderOffScreen` 시작 경로에서는 `FD3D12Viewport::Resize`의 `SwapChain4.GetReference()` ensure를 플러그인 비활성 대조에서도 확인했습니다. CCTV-01G의 비정상 종료 때는 종료 중 Landscape의 `MTAccessDetector` 호출 스택도 남았습니다. 이 자료만으로 스택 오버플로의 원인을 확정하지 않습니다. 재현 횟수·호출 스택과 대조 조건의 차이는 CCTV-01J 결과에 기록합니다. `-RenderOffScreen`을 현장 점검의 기본 실행 방법으로 사용하지 않습니다.

화면이 없는 서버의 연결·디코딩 점검은 위 Linux 예시의 `-nullrhi`를 사용합니다. 이 경로는 DX12 스왑체인과 Landscape 렌더링을 만들지 않으며, 표시·스크린샷은 미검증 WARN으로 남깁니다. 화면이 필요한 수용 검증은 일반 창이나 렌더링 가능한 Linux 환경에서 별도로 수행합니다.

CCTV-01J의 최종 C++ `f6192e9`에서 로컬 점검10개와 16채널30fps 점검을 확인했습니다. 별도 기존 하네스의 16채널60fps 회귀는 이 PC의 일반 창에서 표시48.4848fps로 **FAIL**이 남았습니다. 같은 바이너리·1920×1080·게임60/송출60·100초의 오프스크린 비교는 표시59.8466fps·p95 36ms로 통과했지만, 일반 창의 FAIL을 대체하지 않습니다. 최초 회귀에서는 실행 중 뷰포트가1902×991로 바뀌어 기존 캡처 기준상 무효였고, 정상 크기 재측정에서도 일반 창의 게임 프레임이 함께 낮아졌습니다. 창·표시 경로에 따른 차이의 정확한 원인은 미확정입니다. 실제 현장 화면은 점검 도구의 표시97% 기준으로 확인하며, 이 PC의 일반 창60fps 수용은 완료하지 않았습니다.

CCTV-01J 후속 실제 입력 시험에서는 `OpenInputDesktop`의 입력 데스크톱 이름이 `Default`임을 시작 직전에 확인했습니다. `TestHost/Scripts/check_input_desktop.ps1`로 같은 읽기 전용 확인을 재현할 수 있으며, LogonUI 프로세스 유무만으로 잠금을 판단하지 않습니다. 주소 추가·삭제, 메인/서브·분리 계정과 URL 계정 입력·가림, 시작, 창 최대화/복원, 두 채널의 한 열→두 열 격자 변경, 보고서 생성을 실제 입력으로 확인했습니다. 실제 보고서는 1채널 PASS와2채널 PASS였으며 표시가 약30fps였습니다. 첫 창의 크기 변경 뒤 일부 입력이 반영되지 않은 관측은 원인 미확정으로 보존했고 새 창에서 격자 동작을 확인했습니다. 이 UI 확인은 위 일반 창16채널60fps FAIL을 변경하지 않습니다. 후속으로 기존 TestHost(DTCore 없음)의 Play 버튼을 눌러20초 PIE 재생과 UE 로그를 확인했습니다. 하네스의 자동 전체 종료 요청 뒤 엔진 AssetEditorSubsystem/EndPlayMap에서 접근 위반으로 종료해, 로그 경로 확인은 PASS·정상 종료는 FAIL로 기록했습니다. 제공된 스택에 DTCctv 함수는 없으며 정확한 원인은 미확정입니다. DTCore 존재 PIE 또는 정상 Stop 버튼 경로는 별도 범위입니다.

## 창 상태와 백그라운드 FPS (CCTV-01W)

같은 C++ `f6192e9`에서 W1 포그라운드는 표시59.9037fps, W2 일부 가림·비포커스는50.0652fps, W3 최소화는50.6028fps, W4 오프스크린은59.8483fps였습니다. W2와 같은 창 조건에서 프로세스 전원 제한을 명시적으로 끈 W5는59.9293fps였습니다. 실행 속도 제한만 끄면48.5072fps였지만 **타이머 해상도 요청만 존중하도록 명시하면59.9536fps**로 회복됐습니다. 이 PC에서의 근접 원인은 백그라운드 타이머 해상도 정책으로 좁혀졌습니다. 원래 ControlMask0/StateMask0은 OS 자동 관리이며 내부 휴리스틱의 적용 상태를 직접 읽었다는 뜻은 아닙니다.

일반 창은 전용 포그라운드를 유지하고, 화면 없는 서버는 검증된 오프스크린 경로를 실행 후보로 삼습니다. 실제 Pixel Streaming 동시 인코딩 부하는 별도 검증입니다. 백그라운드에서도60fps가 필요하면 TestHost의 **기본 꺼짐 명시 옵션**으로 타이머 정책을 제어하는 안을 권장하며 현재 미구현·사용자 결정 대기입니다. 플러그인이나 시스템의 기본 전원 정책을 바꾸지 않았고 진단 프로세스의 설정은 원래대로 복원했습니다. API 근거·조건별 수치·실행 스크립트와 선택안의 장단점은 [창 상태 진단 상세](docs/WINDOW_STATE_DIAGNOSTICS.md)에 있습니다.

01J의 일반 창 게이트는 **W1 포그라운드 조건에서 PASS**로 갱신했습니다. 이전 비포커스 여부를 기록하지 못한48.4848fps FAIL과 PIE 종료 FAIL은 보존합니다. 비포커스/최소화도 통과했다는 뜻이 아닙니다.

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

**기존 FFMPEGMedia와의 설치 공존은 플랫폼별로 결과가 다릅니다.** CCTV-01I 재시험에서 `main(819e004)`을 활성화하고 사용하지 않는 Windows 조건의 DTCctv 16채널은 표시 59.9398fps로 통과했습니다. 구 `avdevice-58.dll` 로드 실패는 별도로 남아 있습니다. Linux 공존 패키징은 구 FFmpeg API 미해결 심볼로 실패했습니다. R15 보충 확인에서는 기존 플러그인 단독 Linux 링크가 통과했으며, `.so` 24개가 실제 ELF임을 확인했습니다. Windows의 구 avdevice 오류 126은 DTCctv 없이도 재현됐습니다. 운영본 동기화 브랜치 `feature/dt-project-sync(ea78883)`는 철자 오타 5줄 수정 후에도 오디오 패킷 상수 미선언으로 빌드에 실패했습니다. 자세한 범위와 근거는 [공존 재시험 기록](docs/FFMPEG_MEDIA_COEXISTENCE.md)을 참조하십시오.

Pixel Streaming을 활성화한 Windows·Linux 단독 빌드의 마운트와 16채널 재생을 확인했습니다. 판정 범위는 **"플러그인 로드 공존만 확인, 실제 스트리밍 동시 부하는 미검증"**입니다. Linux 단독 시험은 nullrhi 소프트웨어 디코딩 범위이며, 위 FFMPEGMedia 공존 빌드 실패와 구분합니다.

## Linux 드라이버와 서버 점검

### 네트워크 장애 시험 (CCTV-01K)

CCTV-01K **1차 실행은 ENV_BLOCKED**였습니다. 기존 RHEL8 실행 이미지와 중계 후보를 포함한 로컬 이미지36개에 `tc`가 없어 28개 실측을 수행하지 못했습니다. 원문은 `Saved/Reports/CCTV-01K/first-run-preserved/`에 보존했습니다.

사용자가 공식 Rocky8 `iproute-tc`와 최소 의존 RPM 다운로드를 승인하여 재실행 중입니다. 다운로드9개 합계2,707,640바이트(기존 설치를 확인하기 전에 받은 미사용 libnl3 1개 포함), 설치는 필요한8개로 한정했습니다. URL·크기·SHA256과 공식 공개키 지문·RPM 서명 확인을 기록했습니다. RPM은 읽기 전용 마운트하고 새 시험 컨테이너의 쓰기 계층에만 설치합니다. 기존 이미지·호스트/WSL 네트워크 설정은 변경하지 않습니다. 현재 커널에서 netem·IFB·수신 제어 경로를 확인했으며, Linux28개와 Windows TCP 중계14개를 직렬 측정합니다. 현재 판정은 `Saved/Reports/CCTV-01K/progress.md`와 checkpoint를 참조하십시오. C++와 기본값은 변경하지 않았습니다.

아래 도구는 조건표와 기존 로컬 이미지의 준비 상태를 기록합니다. `--probe`는 자체 검사 컨테이너만 만들고 ID로 정리하며, 패키지를 실행하지 않습니다. 검사 대상에 `tc`가 준비됐더라도 결과 `READY`는 UE 재생 합격을 뜻하지 않습니다.

```powershell
python TestHost/Scripts/network_fault_test.py --out Saved/Reports/CCTV-01K/next-plan
python TestHost/Scripts/network_fault_test.py --out Saved/Reports/CCTV-01K/next-probe --probe
python TestHost/Scripts/network_fault_test.py --out Saved/Reports/CCTV-01K/next-rpm-probe --probe --image ps-node24-rhel8-debug:20261027 --rpm-dir "$env:LOCALAPPDATA\Temp\cctv\dl\rpm-tc"
```

`netem_control.sh`는 자체 시험 컨테이너 안에서 수신 인터페이스를 IFB로 보내 N1~N7을 적용·해제하는 제어 스크립트입니다. 같은 작업에서 새로 만든 Linux 플레이어의 네트워크 네임스페이스를 도구 컨테이너가 공유하는 구성에 사용합니다. 송신 ACK만 제한한 결과를 UDP 영상 손실 시험으로 간주하지 않습니다. 수신 방향을 IFB로 넘기는 방식은 [tc-mirred 매뉴얼](https://man7.org/linux/man-pages/man8/tc-mirred.8.html)의 수신 트래픽 처리 방식을 따릅니다.

준비된 도구 컨테이너에서의 호출 형식은 다음과 같습니다. `$netemCid`는 이 시험에서 직접 생성하고 기록한 컨테이너 ID여야 합니다. 컨테이너는 `NET_ADMIN`, `tc`, `ip`, 커널 netem/IFB 지원, 쓰기 가능한 `/tmp`, `DTCCTV_NETEM_OWNED=1`, `/control/netem_control.sh`의 읽기 전용 마운트가 필요합니다. 호스트 네트워크를 공유하는 컨테이너에는 사용하지 않습니다.

```powershell
docker exec $netemCid /bin/sh /control/netem_control.sh init
docker exec $netemCid /bin/sh /control/netem_control.sh apply N1
docker exec $netemCid /bin/sh /control/netem_control.sh status
docker exec $netemCid /bin/sh /control/netem_control.sh clear
docker exec $netemCid /bin/sh /control/netem_control.sh cleanup
```

N2는150ms/지터30ms, N3는손실0.5%, N4는손실2%, N7은50ms/지터10ms/손실0.5%입니다. N5는 `apply N5 <실측한 전체 스트림 bps>`로80%를 계산합니다. 이80%는 포화 시험 조건이며 권장 회선 사용률이 아닙니다. N6는100% 손실을10초 적용한 뒤 복구합니다. `clear`는 장애 큐를 비우므로 남은 패킷은 버려지며 이 경계도 기록해야 합니다. 각 조건은120초 이상 실행하고 초기10초를 제외하며, 전후 정상 구간에서 같은 프로세스의 메모리·스레드 복귀를 비교합니다. 65,536패킷 큐 한계와 실제 backlog/drop 카운터를 기록해 의도하지 않은 큐 포화를 구분해야 합니다. 지터·손실의 정확한 난수열 재현은 현재 스크립트가 보장하지 않습니다. 옵션 의미와 타이머/큐 제약은 [tc-netem 매뉴얼](https://man7.org/linux/man-pages/man8/tc-netem.8.html)을 참조하십시오.

재실행은 `network_fault_case.ps1`로 실제 `init/apply/clear`와 패키지를 연결하고, `analyze_network_case.py`가 적용 초기10초를 제외한120초 이상을 판정합니다. 원래 기준인 N1/N2/N7의29.1fps·재연결0, N3의 유지·재연결0과 TCP 재전송/UDP 깨짐·회복, N4/N5의 무크래시·무정지·반복 없는 복구, N6의 마지막 화면 유지·복구5초를 유지합니다. Linux NullRHI의 화면·마지막 프레임 유지·표시는 미검증으로 구분하고, 디코딩 오류 카운터를 UDP의 모든 깨진 프레임 수로 대신하지 않습니다. Windows 중계는 Node의 TCP 바이트 중계이며 UDP 중계는 지원하지 않습니다.

Linux UDP는 Docker Desktop의 직접 경로가 성립하지 않아 시험 전용 RTSP 포트 중계를 사용합니다. 실제 RTP/RTCP 데이터그램은 loopback에 게시한 UDP30000~30127을 거쳐 컨테이너의 수신 netem으로 들어가며, 영상을 TCP로 터널링하지 않습니다. 반복 실행에서 일부 채널이 장애 전부터 수신되지 않은 자료는 준비 조건 미성립으로 제외·보존했습니다. 이후 `prepare_netem_owner.py`로 UDP 조건마다 새 소유 네트워크 컨테이너를 만들고, 장애 전16Playing·새 프레임 수신을 필수 확인합니다. 짧은 준비 표본에 FPS97%를 적용하지 않으며, 실제29.1fps 기준은120초 이상 구간에서 그대로 판정합니다.

`write_network_result.py <재실행디렉터리>`는 원시 분석에서 중간 집계를 갱신하고, 측정·검수·정리 후 `--final`로 마감합니다. `network_fault_case.ps1`은 같은 디렉터리의 `configuration.json`과 직접 생성한 소유ID 기록 `network-owner.json`이 필요합니다. 임의의 기존 컨테이너 ID를 넣어 실행하지 않습니다. N6는 제어 세대를 확인하여 취소된 이전 시험이 다음 시험의 qdisc를 늦게 수정하지 않게 합니다.

현재 frame age는 **디코더 반환~렌더 명령 등록**이며 네트워크 지연이 포함되지 않습니다. 여기에서50/150ms를 빼 음수 지연을 만들면 안 됩니다. 네트워크 포함 지연과 정확한 UDP 손상 프레임 수는 현재 미측정이며, 해당 항목을 PASS로 올리지 않습니다. TCP 재전송·재정렬은 같은 네임스페이스의 nstat/ss와 qdisc 원문을 함께 남깁니다. 메모리 전후 비교에는 정상 초기화·캐시 증가도 포함될 수 있으므로 원시 증감과 실제 누적 결함의 해석을 구분합니다.

현장에서는 기존 TCP 기본값을 우선 유지하고 망별 실측 후 UDP와 비교합니다. TCP는 손실 재전송과 순서 보장을 제공하지만, 이것이 CCTV-01K의 성능·회복 합격을 보증하지는 않습니다. [TCP 표준 RFC9293](https://www.rfc-editor.org/rfc/rfc9293.html) 메커니즘과 이번 미측정 상태를 구분하십시오. 대역폭은 실제 전체 스트림의 피크와 전송 오버헤드를 측정해 여유를 정하고, 작은 뷰에는 기존 서브 스트림을 활용하되 장애망에서의 효과는 별도 검증합니다. 읽기 시간 초과5초·재연결·패킷 큐 기본값은 이번 단계에서 바꾸지 않았습니다.

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

## 이전 CCTV-01E 검증 기록

2026-11-06 CCTV-01E의 최종 C++는 `e4a3eb8`입니다. Editor 빌드와 전체 단위14개가 성공했고(일반13·경고 포함 성공1), 새 Win64·Linux 패키지를 만들었습니다. FIFO·생산 대기·HD8장/4K2장 바이트 예산·디코더 반환 시각을 포함해 검증했습니다. FFmpeg 바이너리와 셰이더 소스는 변경하지 않았습니다.

보정한 수정 전 FIFO8의 최악 채널 p95는 게임144/영상60에서11ms,60/60에서91ms,30/60에서143ms,60/30에서19ms였습니다. 처음 CPU 변환 뒤부터 측정한9회는 원수치를 보존하고 최종 합격 근거에서 제외했습니다. 이후 디코더 반환 직후 기준의 격리 패키지로 수정 전4조건을 다시 측정했습니다.

최신 코드의4조건은 모두 통과했습니다. 최악 p95는11/36/82/19ms, 표시는59.961/59.906/29.989/29.988fps입니다. 144/60과60/30의 대체는0,60/60은0.1774%입니다. 별도30fps 보조 측정도 대체0을 확인했습니다. 0.3초 정지 버스트를8장 상한 안에서 대체0으로 보존했고 최대 age312ms도 기록했습니다. 2초 정지 회복은 별도 게이트에서 확인했습니다.

Windows 기본 패키지의 1920×1080 16타일 PNG를 직접 검수했습니다. Linux RHEL8의 SW16과 CUDA4/SW12 디코딩, 운영 점검 스크립트 software-only, Windows·Linux PS 마운트 공존을 최신 패키지로 실행했습니다. Docker NVDEC에는 `--gpus all -e NVIDIA_DRIVER_CAPABILITIES=all`을 사용했습니다. Vulkan은 엔진 RHI 초기화에서 `VK_ERROR_INCOMPATIBLE_DRIVER`·종료139로 끝나 렌더링을 입증하지 못했습니다.

크기5시나리오 PNG를 직접 표본 검수했고 극단 확대 흐림은 저장시각 상한 최대0.754초로 회복했습니다. 연속 PNG 캡처 부하가 있는 표시는 약54fps이므로 기본 FPS 결과와 구분합니다. PNG 없는 별도 subset 측정에서 미변경12채널 표시는59.940fps, 기본59.913fps였습니다. 서버 재시작 후 전16채널 복구는2.672초로5초 목표를 통과했습니다.

Close/Open30회 마지막10회의 Private 기울기는0.782MiB/회, GPU 첫끝 차이는0입니다. 평면 재생성32→32와 RHI 스트리밍/비스토리밍 텍스처 할당량도 고정입니다. 그러나 원시 핸들+2·총 스레드+2로 자원 게이트는 FAIL입니다. 실행파일86/CRT16/NVIDIA 모듈 수는 일정하고 ntdll1~4 변동을 관측했습니다. 이 실패를 누수 없음이나 전체 자원 회복의 합격으로 바꾸지 않았습니다.

사용자는 소크에 한해 처음10분 이후 실행파일·CRT·NVIDIA 시작 TID 집합 일정, OS 모듈에서만 변동, 마지막10분 총수 최대가10~20분 최대를 넘지 않는 기준을 승인했습니다. 이전 총수 min/max도 함께 기록합니다. CCTV-01E 당시에는 Close/Open 기준으로 확장하지 않았고, 이후 CCTV-01F의 사용자 결정에 따라 모듈별 수와 구간 최대 기준을 적용했습니다. 게임60fps 소크는 유효3604.2초, decode59.9989/display59.9355fps, 최악채널p9535ms, 대체3672건(0.1061%), 패킷 손실·재연결·오류0으로 통과했습니다. 실행파일·CRT·NVIDIA TID 집합은 일정하고 변동은ntdll에만 있었으며, 총수 최대는10~20분123개/최종10분122개입니다. 이전 총수 일정 기준은121~123으로 FAIL도 기록했습니다. 10분 이후 Private는+6.449MiB(회귀+2.428MiB/h), GPU Dedicated는−0.0039MiB입니다. Working Set의 큰 감소를 Private 해제나 누수 없음의 증거로 사용하지 않았습니다. 최종 판정은 `Saved/Reports/CCTV-01E/RESULT.md`에 있습니다. 실제 카메라·회사 수용·72시간·실제 Pixel Streaming 동시 인코딩은 미검증입니다.

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

CCTV-01C 측정에서는 `-RenderOffScreen`·1920×1080 PNG와 양수 출력 크기를 확인했습니다. 이후 CCTV-01G의 비정상 종료를 계기로 현장 점검의 기본 실행은 위 일반 창·서버 `-nullrhi` 경로를 사용합니다. `WindowStyle Hidden`은 mediamtx·ffmpeg에만 적용합니다. 첫 Windows 측정은 워밍업 뒤 `output_w > 0`과 최종 PNG의 비검은 픽셀 표본을 확인하며, 0 크기·검은 화면·PNG 읽기 실패는 `INVALID`로 기록합니다. 이 확인은 화면 품질과 전환 깜빡임의 직접 검수를 대신하지 않습니다.

기본 실행기 결과는 `Saved/Reports/CCTV-01/pending-<시각>/`, 큰 로그는 `Saved/Logs/CCTV-01/`에 둡니다. CCTV-01D 결과·로그는 각각 `Saved/Reports/CCTV-01D/`, `Saved/Logs/CCTV-01D/`에 있습니다. 패키지는 저장소 밖 기존 임시 폴더를 사용합니다. Windows 16채널 1080p60 기준은 60초 이상, 평균 디코딩·표시 각각 58fps 이상, 패킷 손실·재연결 0으로 유지합니다. nullrhi는 디코딩 범위만 판정하고 렌더링과 구분합니다. CCTV-01D의 최종 게이트 판정에는 검수 대기 항목이 없습니다.

## CCTV-01F 최종 검증

2026-11-06, 최종 C++ `95cd411`. 소비자 호출의 최근성 350ms·실제 소비 750ms와 렌더 소비 불가 통지를 적용했습니다. 기존 프레임 큐 8장·세션별 24MiB 예산과 패킷 큐 상한을 유지했습니다. 전체 단위 15개가 Success(일반 14+경고 성공 1)이고, 확정 코드로 Editor와 Win64/Linux 패키징을 다시 수행했습니다.

수정 전 GT 3/5/10초 및 렌더 5초 정지에서 패킷 손실 2092/4396/7900/4372개와 재연결 각 4회를 재현했습니다. 최종 새 패키지에서는 네 조건 모두 패킷 손실·재연결·키프레임 대기 0, 회복 상한 1.001/1.007/1.004/1.005초 및 정상 PNG를 확인했습니다. 4개 FPS 조합과 0.3초 버스트·2초 회복·144/30 보조 측정도 통과했습니다. 144/30과 144/60·60/30의 프레임 대체는 0입니다.

| 100회 자료 | Private 후반 기울기(MiB/회) | GPU 후반 차이(MiB) | 판정 |
|---|---:|---:|---|
| 완료된 첫 `convergence100-fix1` | 0.150287 | -0.007812 | 핸들 최대 1692→1696으로 FAIL |
| 최종 정상 `final-close100` | -0.108043 | +0.121094 | GPU 비증가 FAIL |
| LLM 진단 100회 | 0.110853 | +0.011719 | GPU 비증가 FAIL |

첫 100회는 재측정하지 않고 원자료로 판정했습니다. 최종 정상 100회의 승인 핸들 최대는 1681→1679, 스레드 최대는 123→122이고 실행 파일·CRT·NVIDIA 모듈별 수는 일정합니다. 평면 재생성 32→32 및 RHI 텍스처 할당도 고정입니다. LLM 진단에서 Graphics 415.86MiB·Textures 194.50MiB·RenderTargets 93.70MiB가 고정이지만 WDDM GPU 계정값은 증가했습니다. 정확한 추가 할당 귀속은 미확정이며 NVML PID 메모리량도 N/A입니다. 작은 증가에 허용치를 넣거나 평탄한 다른 태그로 GPU FAIL을 바꾸지 않았습니다. E의 30회 승인 기준 재판정은 PASS이나 제외 후 20회이고 비교 구간이 겹칩니다.

크기 변경 5개 시나리오의 PNG를 직접 검수했습니다. 확대 흐림 회복 상한은 0.856초입니다. 무캡처 subset의 미변경 12채널 표시 평균은 59.960fps, 기본은 59.912fps입니다. 서버 복구는 4.109초로 5초 기준을 통과했습니다. Linux SW16·NVDEC CUDA4/SW12 및 Windows/Linux Pixel Streaming 마운트·16Playing이 통과했습니다. Vulkan과 add-host 변형은 엔진 RHI 초기화의 VK_ERROR_INCOMPATIBLE_DRIVER로 ENV_BLOCKED입니다. Pixel Streaming은 실제 클라이언트·동시 인코딩 부하를 포함하지 않습니다.

게임 60fps 소크는 유효 3604.7초, decode 59.999376/display 59.859229fps, 최악 p95 35ms, 대체 8073건(0.2333%), 손실·재연결·오류·키프레임 대기 0으로 통과했습니다. 실행 파일·CRT·NVIDIA TID 집합은 일정하고 변동은 ntdll에만 있으며 총수 최대는 10~20분 122→마지막 10분 121입니다. 이전 총수 일정 기준은 120~122로 FAIL도 보존했습니다. 10분 이후 Private 차이 -1.738281MiB·회귀 1.651334MiB/h, GPU 차이 +0.117188MiB는 추세 관측이며 누수 없음의 증명이 아닙니다. 소크의 메모리 추세 관측과 Close/Open의 GPU 비증가 기준을 구분합니다.

전체 판정은 **FAIL**입니다. 원자료·최초 실패·수정 이력·정리 감사는 `Saved/Reports/CCTV-01F/RESULT.md`와 `result.json`에 있습니다. 실제 16대 카메라·회사 수용·72시간·실제 Pixel Streaming 동시 인코딩은 미검증입니다.

## CCTV-01F의 Close/Open 판정

2026-11-06 사용자 결정에 따라 Close/Open은 처음 10회를 제외한 재생 구간 끝 표본의 모듈별 스레드 수로 판정합니다. 실행 파일·ucrtbase·NVIDIA 모듈의 수는 모든 회차에서 같아야 하며, 변동은 OS 모듈에서만 허용합니다. 핸들과 총 스레드는 마지막 30회의 최대가 11~40회의 최대를 넘지 않아야 합니다. 이전 원시 증감과 판정도 함께 남깁니다. 소크의 TID 집합 기준과 Close/Open의 모듈별 개수 기준은 각각 적용합니다.

100회 Private는 마지막 30회 기울기 0.3MiB/회 이하 또는 마지막 세 10회 구간의 평균 비증가, GPU·RHI 텍스처 할당은 비증가 기준입니다. 이전 E의 30회 재판정은 제외 후 20회이고 비교 구간이 겹치므로 100회 수렴 증거를 대체하지 않습니다. F의 최종 결과는 `Saved/Reports/CCTV-01F/RESULT.md`와 `result.json`에 기록합니다.

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

설계와 미검증 항목은 [docs/DESIGN.md](docs/DESIGN.md), 이번 작업 결과는 `Saved/Reports/CCTV-01F/RESULT.md`를 참조하십시오.
