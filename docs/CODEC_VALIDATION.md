# CCTV-01G 코덱 검증

이 자료는 Ryzen 7 7800X3D(8코어16스레드), RTX4070Ti 12GiB, Windows 및 로컬 RHEL8.10 컨테이너의 합성 파일 반복 송출 결과입니다. 한 RTSP 경로를16클라이언트가 읽었습니다. 입력은사전인코딩 후복사송출했으며GPU인코더0%를확인했습니다. 판정은전체/채널별원본97%,손실·오류·재연결0,Windows대체율≤1%,p95≤70ms(30fps)/50ms(60fps),PNG직접검수입니다.

## 코덱별 결과

Windows는 decode/display fps, Linux는 decode fps입니다. Linux nullrhi의 표시·색·PNG·age는 미검증입니다.

| 조건 | Windows | Linux SW16 | Linux Auto 최대HW4 |
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

C1~C7·C9의 정상 협상 기본 자료는 F최종C++95cd411입니다. 실패형식협상만 수정한9c98d85에서는 영향을받는MJPEG를새패키지로재측정하고,지연4조건·정지·패키지·장애·소크를별도로회귀합니다. 각숫자의코드버전과원자료는Saved/Reports/CCTV-01G/codec-matrix.json에있습니다. 이전자료를새코드의새측정으로표시하지않습니다.

C7 Linux SW16 기본설정의최저채널27.829fps를실패로보존했습니다. 실제틱60·SoftwareDecodeThreads=1 대조에서는채널별97%까지통과했으나CUDA혼합에서스레드1은미달이어서전체기본값을바꾸지않았습니다. CUDA4/SW12는스레드2 대조가통과했습니다.

MJPEG의CUDA형식초기화는이장치·라이브러리·4:2:2시료에서실패했고실제SW16으로폴백했습니다. HW4/SW12성공으로표시하지않습니다. 정상SW형식이제공됐는데도RTSP재접속을요청하던문제를고쳐11회→0회로확인했습니다. 실제사용가능형식이없으면실패신호를유지하며같은연결에서는실패한HW협상을반복하지않습니다. 수정후Windows p9573ms>70,스레드1대조81ms, Linux의97%미달은실패로남습니다. 단일파일에서변환을포함한CPU비용이증가했으나16RTSP의전달·스케줄링·큐대기까지완전히분해한원인확정과는구분합니다.

## 지원 구성과 권장 설정

포함된Windows DLL·Linux SO의 h264/hevc/mjpeg/mpeg4/vp9/av1 디코더를직접조회했습니다. AV1기본은libdav1d입니다. mpeg4·VP9·AV1은구성만확인했고실시간행렬은미수행입니다. Windows CUDA/D3D11VA와RHEL8 CUDA의실제Main10디코딩·P010다운로드를확인했습니다. 플러그인은P010·4:2:2를swscale NV12로변환하며HDR톤매핑검증은포함하지않습니다.

- 본시험의통과입력설정을기준으로주1080p30 H264 High 또는H265 Main,8비트4:2:0,GOP2초를시작점으로권장합니다. H264/H265 B2도검증했습니다. 60fps와Main10은위표의성공범위와CPU부하를확인해야합니다.
- GOP12초는새연결의첫렌더등록/재생에약8~9초가걸린표본이있습니다. 빠른첫화면이필요하면GOP2초와상시연결을권장합니다. 벤더H264+/H265+전체동작을검증한것은아닙니다.
- 작은뷰는H264 640x360서브,큰뷰만H265 4K메인으로쓰는C9구성이통과했습니다. 실제카메라주소는운영프로젝트의SubUrl에넣습니다.
- 하드웨어예산은프로젝트설정 `Plugins → DT CCTV → MaxHardwareDecodeStreams` 또는 `[/Script/DTCctv.DTCctvSettings]`의같은키로조정합니다. H265기본4혼합경로가통과했으므로전역기본4를유지했습니다. 숫자를올려도미지원MJPEG형식이HW가능으로바뀌지는않습니다.
- 소프트웨어스레드는같은설정의 `SoftwareDecodeThreads`입니다. C7 Linux SW전용에서틱60·스레드1을조건부권장하며혼합/H264전체에일괄적용하지않습니다.
- MJPEG16채널30fps는본PC의위미달을수용해야하거나별도서버/입력설정검증이필요합니다. 해상도·FPS를낮춘새조건이나카메라H264대안을원래MJPEG조건의PASS로간주하지않습니다.

운영서버사양이제공되지않아확정성능예측은하지않았습니다. 16x4K30은약3.981Gpixel/s,97%기준전체465.6fps와각채널29.1fps를동시에만족하는실측처리량이필요합니다. 실제카메라복잡도·네트워크·Vulkan렌더링·동시PixelStreaming부하는별도수용항목입니다.

## Linux 운영 점검

기존인자없는GPU/60fps점검과 `--software-only` 동작을보존했습니다. 코덱옵션을지정하면100초실행·처음10초제외80초이상·원본97%·채널별FPS·코덱일치·오류0을판정합니다.

```bash
TestHost/Scripts/linux_server_check.sh /path/to/package "$CAMERA_URL" --software-only --codec hevc --fps 30
TestHost/Scripts/linux_server_check.sh /path/to/package "$CAMERA_URL" --decode-only --codec hevc --fps 30
```

`--decode-only`는nullrhi Auto CUDA4/SW12범위이며렌더링성공을뜻하지않습니다. 두옵션은Main10시료로실행해통과했습니다. 기존60fps기본분석호환과잘못된코덱거부도검증했습니다. URL은환경변수경로로전달하고로그는가립니다. 원격실GPU서버의Vulkan은본작업에서ENV_BLOCKED를계승합니다.
