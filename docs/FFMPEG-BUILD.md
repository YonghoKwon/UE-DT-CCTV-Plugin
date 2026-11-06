# CCTV-01H FFmpeg 경량 빌드 검토

검토일은 2026-11-06이며 기준 저장소 커밋은 `b543646`이다. 권장안은 **동일 FFmpeg 8.1 소스의 필요한 구성만 직접 configure하는 방식**이다. Windows x64는 기존 MSVC와 MSYS2 빌드 도구를 사용하고, Linux x64는 전용 Rocky/RHEL 8 빌더에서 만든다. 빌드 환경과 의존성을 버전·해시로 고정한다.

이번 산출물은 정적 조사와 후속 실행 계획이다. H의 검토 완료와 새 바이너리의 기능·성능 판정은 별도 상태로 관리한다. 새 빌드·라이선스 실출력·용량·하드웨어 실행 검증은 `NOT_RUN`이다. 승인 대상은 3절의 다운로드와 7절의 격리 빌드·검증 범위다.

## 1. 현재 구성과 실제 필요 기능

### 1.1 조사 근거

동봉 버전은 `n8.1.3-34-ga35157abb9-20261104`이며 양 플랫폼의 G1 구성 문자열에 `--enable-version3`가 있다. 현재 라이선스 출력은 LGPL v3 이상이다. Windows와 Linux 각각 avcodec·avformat·avutil·swscale·swresample 5개를 배포한다. 출처·원본 아카이브 SHA256은 기존 ThirdParty README에 있다. [기존 배포 기록](../Source/ThirdParty/DTCctvFFmpeg/README.md)

로컬 DLL/SO를 읽어 얻은 런타임 파일 합계는 **267,108,896바이트, 254.735MiB**다. 이 수치는 10개 런타임 파일의 비압축 크기이며 헤더·import library·Git LFS 전송량은 별도다. G1 기록의 라이브러리 구성과 G3의 27개 코덱 조합을 함께 확인했다. [G 결과 요약](CODEC_VALIDATION.md)

| 플랫폼 | avcodec | avformat | avutil | swscale | swresample | 단위 |
|---|---:|---:|---:|---:|---:|---|
| Windows | 91,620,864 | 19,846,144 | 3,021,824 | 12,972,544 | 747,008 | 바이트 |
| Linux | 93,024,112 | 23,313,016 | 8,613,544 | 13,447,112 | 502,728 | 바이트 |

현재 avcodec은 PE import와 ELF `DT_NEEDED` 모두에서 swresample을 참조한다. 플러그인은 swresample 함수를 직접 호출하지 않는다. 후속 빌드에서 오디오 구성을 제거한 뒤 전이 의존성 소멸을 확인하고 Build.cs의 링크·스테이징 목록을 함께 조정해야 한다. 현재 파일 하나만 제거하면 로드가 실패한다. [FFmpeg 연결 설정](../Source/ThirdParty/DTCctvFFmpeg/DTCctvFFmpeg.Build.cs)

Linux 정적 조사에서는 최대 공개 요구 버전 `GLIBC_2.28`과 avcodec의 `GLIBC_PRIVATE` 참조가 관측됐다. G는 RHEL 8.10 실제 디코딩을 통과했다. 새 산출물은 모든 ELF 의존성의 버전 요구를 조사하고 RHEL 8.10에서 실제 로드해야 한다. `GLIBC_PRIVATE`가 새 빌드에도 있으면 기호·라이브러리를 추적해 이식성을 해결할 때까지 교체 판정을 보류한다. 원시 파일별 해시·import·버전 요구는 `Saved/Reports/CCTV-01H/current-inventory.json`에 저장했다.

### 1.2 코드에서 쓰는 API와 구성

| 영역 | 실제 호출·동작 | 경량 빌드에 남길 구성 |
|---|---|---|
| 연결·디먹싱 | `avformat_network_init`, `avformat_open_input`, `avformat_find_stream_info`, `av_find_best_stream`, `av_read_frame` | avformat, RTSP 수신, RTP·SDP 해석, TCP interleaved·UDP/RTP/RTCP |
| 입력 옵션 | `rtsp_transport=tcp/udp`, `allowed_media_types=video`, timeout, 낮은 지연·분석 크기 옵션, interrupt callback | 네트워크·읽기 취소·프레임 탐색 동작 유지 |
| 디코딩 | `avcodec_find_decoder(codec_id)`, `avcodec_parameters_to_context`, send/receive, flush | 기본 native `h264`, `hevc`, `mjpeg`와 각 parser |
| 하드웨어 | `avcodec_get_hw_config`, `av_hwdevice_ctx_create`, `av_hwframe_transfer_data` | CUDA/NVDEC, Windows D3D11VA, Linux VAAPI |
| 프레임·자원 | AVFrame/AVPacket 참조·복제·해제, `av_pix_fmt_desc_get`, dict·로그·에러 변환 | avutil과 픽셀 형식 설명·하드웨어 컨텍스트 |
| 변환·축소 | `sws_getCachedContext`, `sws_scale`, BILINEAR/FAST_BILINEAR | swscale, x86 SIMD와 런타임 CPU 판별 |

Windows 하드웨어 시도 순서는 CUDA→D3D11VA이며 Linux는 CUDA→VAAPI다. native decoder를 찾은 뒤 HW config를 선택한다. 별도 `h264_cuvid` 같은 이름의 decoder를 고르는 방식으로 바꾸면 현재 경로와 차이가 생긴다. 초안은 native decoder에 결합된 `*_nvdec` hwaccel을 사용한다. [세션 구현](../Source/DTCctv/Private/DTCctvSession.cpp)

NV12·YUV420P·YUVJ420P는 직접 렌더링하며, P010 계열 Main10과 YUVJ422P 등 4:2:2는 시스템 메모리 전송 후 swscale NV12 변환을 거친다. Main10 10비트·MJPEG 4:2:2 입력 지원과 축소 시 색·범위·비율을 재검증한다. 픽셀 형식을 8비트 4:2:0만 지원하도록 축소하면 G6·G8 경로를 잃는다. [프레임 처리](../Source/DTCctv/Private/DTCctvSession.cpp)

### 1.3 CCTV-01G를 반영한 코덱 범위

| 입력 | G의 확인 범위 | 경량 프로파일 |
|---|---|---|
| H.264 High | C1 B2·GOP2초, C2 GOP12초, C9 640×360 서브 | native h264와 parser, NVDEC·D3D11VA·VAAPI |
| H.265 Main/Main10 | C3~C7의 1080p30/60·B2·긴 GOP·10비트·4K, C9 메인 | native hevc와 parser, NVDEC·D3D11VA·VAAPI, P010→NV12 |
| MJPEG 4:2:2 | C8의 정상 SW 폴백·색, Windows 지연과 Linux FPS 미달 | native mjpeg와 parser, NVDEC·VAAPI 구성, SW16 폴백과 4:2:2→NV12 |
| MPEG4·VP9·AV1 | G1의 라이브러리 구성 조회 | 기본 경량 프로파일에서 제외하는 승인 제안 |

G는 PASS 23개·FAIL 4개다. C8의 Windows p95 73ms와 Linux 약27fps, C7 기본 Linux SW 최저27.829fps는 기존 실패다. MJPEG도 요구 입력이므로 디코더를 유지한다. `libdav1d`가 기본 AV1 decoder인 현 구성까지 보존하려면 별도 호환 프로파일과 추가 의존성 검토가 필요하다. native AV1로 대체할 때는 성능 비교가 추가된다. [코덱별 실측](CODEC_VALIDATION.md)

플러그인은 URL을 avformat에 넘기고 decoder를 codec ID로 찾는다. 현재 넓은 라이브러리 구성이 수용하던 다른 포맷·오디오·RTSPS/TLS는 G에서 실사용을 입증하지 않았다. 이번 기본 프로파일의 기능 동등 범위는 H가 지정한 RTSP TCP/UDP·영상 3코덱이다. 운영에서 추가 코덱이나 RTSPS가 필요하면 다운로드 승인 전에 범위를 확정해야 한다.

## 2. 빌드 방식 비교와 권장안

아래 전송량은 압축 다운로드의 계획 범위다. 패키지 의존성·캐시·플랫폼별 digest가 확정되면 별도 lock manifest에 URL·바이트·SHA256을 기록한다.

| 방식 | 필요한 다운로드·출처와 계획량 | 재현성과 glibc 2.28 | 유지보수·통합 부담 | 판단 |
|---|---|---|---|---|
| BtbN 사용자 정의 variant/addin | FFmpeg-Builds MIT 소스, MinGW·Linux 크로스 빌더 이미지, 선택 의존성 소스. 이미지·도구 체인 포함 2~8GiB 추정, 디스크10~30GiB | Linux 공식 대상은 RHEL/CentOS8·glibc2.28. 빌더 commit·모든 image digest·소스 SHA·날짜 입력 고정 필요 | 기본 LGPL variant가 version3와 LGPLv3 고지를 지정. 옵션 외에 패키지 활성화와 LICENSE 복사를 함께 수정해야 함 | 대안 A |
| 직접 configure, Windows MSYS2/UCRT64 MinGW | MSYS2 40.9MB, GCC 본체46.28MB와 binutils·CRT·headers·winpthreads 등. 도구 전체150~400MiB 추정 | 각 패키지 버전·해시 고정. Linux는 별도 RHEL8 빌더 필요 | 기존 BtbN과 GCC 계열 일치. lib.exe import 생성과 pthread·GCC 런타임 의존성 확인 필요 | 대안 B |
| 직접 configure, Linux→Windows MinGW 교차 | RHEL8 빌더·MinGW 크로스 툴체인·sysroot·NASM·소스. 0.5~2GiB 추정 | Linux native sysroot2.28과 Windows sysroot를 따로 고정 | 한 빌더 운영 가능. SDK/header·import library·Windows 실기기 QA 관리 필요 | 대안 C |
| **직접 configure, Windows MSVC + Linux 전용 RHEL8** | 공통 소스·nv headers, MSYS2 최소 도구, RHEL8 이미지와 RPM. 기존 MSVC 재사용 가정 총0.3~0.8GiB 추정 | Windows 컴파일러·SDK 버전 고정, Linux digest·RPM NEVRA·해시 고정, 전체 의존성 ABI 검사 | MSVC import library가 UE와 맞음. Windows pthread→w32threads 변화와 Linux VAAPI 런타임을 별도 검증 | **권장안** |
| 기존 광범위 배포 유지 | 신규 경량 빌드 전송0 | G의 현 바이너리와 RHEL8 증거 보유 | 현재 크기·LGPLv3·광범위 코드 유지 | 경량 목표 달성 전 기준 패키지 |

BtbN은 bash·Docker 빌드를 제공하며 `lgpl-shared`도 기본 LGPL 구성에 `--enable-version3`가 있다. 전용 변형에서는 이 옵션과 `COPYING.LGPLv3` 선택, version3가 필요한 외부 라이브러리 활성화를 함께 제거해야 한다. [BtbN 빌드 안내](https://raw.githubusercontent.com/BtbN/FFmpeg-Builds/master/README.md), [기본 LGPL 변형](https://raw.githubusercontent.com/BtbN/FFmpeg-Builds/master/variants/defaults-lgpl.sh)

권장안의 Windows는 MSYS2를 셸·make·pkgconf·NASM 용도로 쓰고 기존 MSVC로 DLL과 import library를 만든다. FFmpeg 공식 문서가 MSVC·MSYS2·NASM 경로를 제공한다. MinGW 대안도 `lib.exe /machine:x64 /def:… /out:…`로 import library를 만들어 UE의 정상 링크 최적화를 유지한다. [플랫폼 빌드 안내](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/doc/platform.texi)

Linux는 기존 다른 작업의 컨테이너·이미지와 분리한 전용 빌더와 검증 컨테이너를 사용한다. 승인 후보는 `rockylinux/rockylinux:8.10`의 amd64 `sha256:f5529992e67440c1a4ae7788244d4381c6909159a88eacd95b7523ae47ced82e`다. 태그 메타데이터 갱신일은2024-06-05이므로 새 빌더에서 서명된8계열 보안 RPM snapshot을 적용하고 패키지 잠금 목록을 보존한다. 현재 공개 `library/rockylinux` 안내는 최신 이미지 출처로 프로젝트 저장소를 가리키므로 레지스트리 경로도 함께 고정한다. [Rocky 이미지 안내](https://hub.docker.com/_/rockylinux)

## 3. 다운로드 승인 목록

`확인`은 게시된 메타데이터 크기이며 `추정`은 예산이다. 아카이브·패키지·이미지 전송은 후속 승인 단계다. API·공식 문서 열람으로 원문의 옵션과 공개 메타데이터를 조사했다.

| ID | 대상·출처 URL | 크기 | 라이선스·용도 |
|---|---|---|---|
| D1 | [FFmpeg 동일 커밋 소스](https://github.com/FFmpeg/FFmpeg/archive/a35157abb96167fd7308edc92b55408d5bb084b0.tar.gz) | 20~40MiB 추정 | LGPL2.1+ 중심, 일부 MIT/BSD/IJG 고지. 양 플랫폼 공통 |
| D2 | [nv-codec-headers n13.0.19.0](https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.0.19.0.tar.gz) | 0.1~0.3MiB 추정 | MIT, ffnvcodec 헤더·동적 GPU API 로더 |
| D3 | [MSYS2 base x64 20260927](https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.tar.xz) | 42,860,696바이트 확인 | 구성 패키지별 GPL/LGPL/BSD 등. 빌드 도구 전용 |
| D4 | [make 4.4.1-3](https://mirror.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst) | 0.49MB 확인 | GPL3, 빌드 도구 |
| D5 | [pkgconf 3.0.10-1](https://mirror.msys2.org/msys/x86_64/pkgconf-3.0.10-1-x86_64.pkg.tar.zst) | 0.12MB 확인 | ISC, 빌드 도구 |
| D6 | [NASM 3.02-1](https://mirror.msys2.org/msys/x86_64/nasm-3.02-1-x86_64.pkg.tar.zst) | 0.40MB 확인 | BSD-2-Clause, SIMD 어셈블리 도구 |
| D7 | [MSYS2 패키지 저장소](https://repo.msys2.org/msys/x86_64/)의 bash·coreutils·sed·grep·awk·diffutils와 D4 전이 의존성 | 추가10~50MiB 추정, base 중복분 제외 | GPL·LGPL·BSD 등 패키지별. 정확한 전이 목록·해시 고정 필요 |
| D8 | [Rocky Linux 8.10 이미지](https://hub.docker.com/r/rockylinux/rockylinux) `docker.io/rockylinux/rockylinux:8.10` | 압축76,611,732바이트 확인 | RPM별 GPL/LGPL/BSD 등. amd64 digest·압축 layer 합계를 승인 목록에 고정 |
| D9 | [Rocky8 AppStream](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/), [BaseOS](https://download.rockylinux.org/pub/rocky/8/BaseOS/x86_64/os/Packages/)의 gcc·binutils·glibc-devel·make·pkgconf·nasm·기본 셸 도구와 의존성 | 100~350MiB 추정, 이미지 포함분 제외 | GCC GPL3+Runtime Library Exception, binutils/Make GPL, glibc LGPL2.1+, NASM BSD 등 |
| D10 | [libva-devel 2.13.0-2.el8 x64](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/l/libva-devel-2.13.0-2.el8.x86_64.rpm) | 130,340바이트 확인 | MIT, VAAPI 헤더·pkg-config |
| D11 | [libva 2.13.0-2.el8 x64](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/l/libva-2.13.0-2.el8.x86_64.rpm) | 105,392바이트 확인 | MIT, libva.so.2·libva-drm.so.2 런타임 후보 |
| D12 | [libdrm-devel 2.4.115-2.el8 x64](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/l/libdrm-devel-2.4.115-2.el8.x86_64.rpm) | 187,244바이트 확인 | MIT 중심, DRM 헤더·pkg-config |
| D13 | [libdrm 2.4.115-2.el8 x64](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/l/libdrm-2.4.115-2.el8.x86_64.rpm) | 167,392바이트 확인 | MIT 중심, libdrm.so.2 런타임 후보 |
| D14 | D10~D13의 X11·libpciaccess 등 선택 RPM·전이 의존성과 대응 소스 RPM | 추가1~20MiB 추정. D9와 중복 합산 제외 | MIT/BSD 계열 중심, RPM 라이선스·필요 ELF를 실제로 전개해 확정 |

파일 조사에서 MSVC14.38.33130·14.44.35207 등의 x64 cl.exe와 Windows SDK10.0.22621.0·10.0.26100.0 설치를 확인했다. 권장 시험의 우선 후보는 MSVC14.38.33130과 SDK10.0.22621.0이며, 정확한 컴파일러 patch 버전과 사용 계약은 승인 단계에서 lock에 기록한다. 기존 Git·Python·Docker Desktop도 재사용 후보이며 설치 라이선스 수용 여부는 별도 확인한다. 재설치가 필요하면 별도 다운로드 목록으로 올린다. MinGW 대안에서는 [GCC 16.2.0-6](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-gcc) 본체46.28MB와 전이 도구 체인을 추가 승인한다. BtbN 대안은 [FFmpeg-Builds MIT 소스](https://github.com/BtbN/FFmpeg-Builds)와 해당 빌더 이미지·sysroot 전체가 추가 승인 대상이다.

D1의 전체 소스 커밋은 GitHub 커밋 메타데이터로 `a35157abb96167fd7308edc92b55408d5bb084b0`을 확인했다. 커밋 시각은2026-11-04 05:30:17 UTC다. D2 tag의 commit은 `e844e5b26f46bb77479f063029595293aa8f812d`다. D3 asset digest는 `sha256:ea2f31a0b6ade63914ce441ffb022f0f6aa96982bfefa2326460a26d5fb01322`다. 소스 아카이브 전송 후의 SHA256과 전체 source/object 일치는 승인 단계에서 확인한다. configure 초안은 공개 `release/8.1` 원문을 대조했으며, 고정한 실소스에서 옵션과 최종 의존성을 다시 검증해야 한다. [FFmpeg 고정 커밋](https://github.com/FFmpeg/FFmpeg/commit/a35157abb96167fd7308edc92b55408d5bb084b0), [NVIDIA 헤더 고정 태그](https://github.com/FFmpeg/nv-codec-headers/tree/n13.0.19.0)

D8의 digest·압축 전송 크기는 공개 태그 메타데이터로 확인했다. D1/D2 아카이브의 전송 크기와 D7/D9/D14의 전체 전이 목록은 미확정이다. 후속 승인 제안은 **새 다운로드 총0.8GiB 상한의 격리 시험**, 부족 시 목록을 갱신하는 방식이다. 최초 환경의 작업 디스크는 소스·양 플랫폼 산출물·패키지 캐시를 포함해5~10GiB를 계획한다. 이 상한과 예상 크기는 측정값과 구분한다.

공개 크기 근거는 MSYS2 release asset 목록·패키지 페이지와 Rocky RPM 인덱스다. [MSYS2 base 메타데이터](https://github.com/msys2/msys2-installer/releases/expanded_assets/2026-09-27), [Make](https://packages.msys2.org/packages/make), [pkgconf](https://packages.msys2.org/packages/pkgconf), [NASM](https://packages.msys2.org/packages/nasm), [Rocky RPM 인덱스](https://download.rockylinux.org/pub/rocky/8/AppStream/x86_64/os/Packages/l/)

## 4. configure 초안

아래는 승인 후 실소스에서 `configure --help`·`--list-*`와 의존성 전개를 확인할 초안이다. H에서는 실행하지 않았다. `DTCCTV_PREFIX`와 `DTCCTV_NV_PREFIX`는 별도 작업 디렉터리의 설치 경로다. 공통 옵션과 플랫폼 옵션을 한 configure 호출에 결합한다. Linux의 `\$$ORIGIN`은 configure→Make→셸 전달을 위한 초안 표기다. 실제 ELF의 RPATH/RUNPATH에 리터럴 `$ORIGIN`이 남았는지 확인하며 전이 SO의 검색 경로까지 검사한다.

```bash
COMMON=(
  --disable-everything
  --disable-autodetect
  --disable-gpl --disable-version3 --disable-nonfree
  --enable-shared --disable-static --enable-pic
  --disable-debug --disable-doc --disable-programs
  --enable-avutil --enable-avcodec --enable-avformat --enable-swscale
  --disable-avdevice --disable-avfilter --disable-swresample
  --enable-network
  --enable-demuxer=rtsp,rtp,sdp
  --enable-protocol=tcp,udp,rtp,http
  --enable-decoder=h264,hevc,mjpeg
  --enable-parser=h264,hevc,mjpeg
  --enable-bsf=extract_extradata,h264_mp4toannexb,hevc_mp4toannexb
  --enable-ffnvcodec --enable-nvdec
  --disable-cuvid --disable-nvenc --disable-cuda-llvm --disable-cuda-nvcc
  --enable-asm --enable-x86asm --enable-runtime-cpudetect
)
```

```bash
# Windows. x64 MSVC 개발 셸에서 MSYS2 셸을 연 환경을 사용한다.
PKG_CONFIG_PATH="$DTCCTV_NV_PREFIX/lib/pkgconfig" ./configure \
  "${COMMON[@]}" --prefix="$DTCCTV_PREFIX" \
  --toolchain=msvc --arch=x86_64 \
  --enable-w32threads --disable-pthreads \
  --enable-d3d11va --disable-vaapi \
  --enable-hwaccel=h264_nvdec,hevc_nvdec,mjpeg_nvdec,h264_d3d11va,h264_d3d11va2,hevc_d3d11va,hevc_d3d11va2
```

```bash
# Linux. RHEL8/glibc2.28 빌더 안의 gcc·binutils·libva-devel·libdrm-devel.
PKG_CONFIG_PATH="$DTCCTV_NV_PREFIX/lib/pkgconfig" ./configure \
  "${COMMON[@]}" --prefix="$DTCCTV_PREFIX" \
  --arch=x86_64 --target-os=linux \
  --enable-pthreads --disable-w32threads \
  --enable-libdrm --enable-vaapi --disable-xlib \
  --enable-hwaccel=h264_nvdec,hevc_nvdec,mjpeg_nvdec,h264_vaapi,hevc_vaapi,mjpeg_vaapi \
  --extra-ldflags='-Wl,-rpath,\$$ORIGIN'
```

`nv-codec-headers`는 공통 prefix에 헤더·pkg-config를 설치하는 준비 의존성이다. NVDEC는 드라이버 API를 동적으로 로드한다. 이 구성의 CUDA 컴파일 커널·NPP 필터·NVENC 인코더는 필요 목록에 없다. n13.0.19.0 README의 드라이버 기준은 Windows/Linux570 이상이며, 운영 장치의 실제 NVDEC 초기화·P010 다운로드를 추가 확인한다. [드라이버 기준](https://raw.githubusercontent.com/FFmpeg/nv-codec-headers/n13.0.19.0/README), [MIT 헤더·동적 로더](https://raw.githubusercontent.com/FFmpeg/nv-codec-headers/n13.0.19.0/include/ffnvcodec/dynlink_loader.h)

RTSP는 FFmpeg의 demuxer이므로 `--enable-demuxer=rtsp`를 사용한다. 8.1 configure에서 RTSP는 HTTP와 rtpdec를 선택하고 rtpdec는 ASF·MOV·MPEGTS·RM·RTP·SRTP 관련 구성을 선택한다. 따라서 공통 목록은 최상위 요청이며 최종 활성 목록은 `config_components.h`·`config.h`에서 추출해 검수한다. 자동 선택된 구성의 제거를 원하면 별도 소스 변경과 RTSP 호환 검증이 필요하다. [configure 의존성 정의](https://raw.githubusercontent.com/FFmpeg/FFmpeg/release/8.1/configure)

native h264/hevc/mjpeg, Main10·4:2:2 변환, SIMD와 런타임 CPU 판별을 유지한다. `--enable-small`의 크기 우선 최적화는 성능 동등 목표와 별도 실험 항목이다. CLI·인코더·파일 demuxer가 필요한 G의 시료 생성·ffprobe 검사는 기존 승인 도구로 수행하고, 경량 라이브러리 검사는 패키지와 연결된 C API·TestHost 경로로 수행한다.

## 5. 예상 효과와 라이선스·보안

### 5.1 용량 계획

목표 비압축 런타임 예산은 **양 플랫폼 합계60~100MiB**다. 현254.735MiB 대비 약60.7~76.4% 감소에 해당한다. 이는 빌드 전 공학적 추정이며 실측 상태는 `NOT_RUN`이다. 현재 avcodec 두 파일만176.09MiB이며, 전체 코덱·인코더·외부 정적 코드가 포함돼 있다. swscale 두 파일은25.20MiB여서 이 부분과 필요한 SIMD·색 변환의 잔존 비용을 고려해 범위를 넓게 잡았다.

링커 map·각 DLL/SO strip 전후 크기·실제 스테이징 합계·압축 패키지 크기를 후속 단계에서 수집한다. import library·헤더·소스와 runtime 파일의 크기를 따로 보고한다. 용량 목표는 기존 FPS·지연·HW 게이트의 대체 기준으로 쓰지 않는다.

### 5.2 고지 목록

| 범주 | 후속 배포 고지 계획 |
|---|---|
| FFmpeg | 해당 소스의 `COPYING.LGPLv2.1`, copyright·LICENSE.md, 수정 내역, 전체 소스·패치·configure·빌드 도구 버전·재현 절차 제공 경로 |
| JPEG 코드 | native MJPEG에 적용되는 IJG 저작권·고지와 해당 코드 수정 내역 |
| NVIDIA 헤더 | MIT 저작권·허가문. 라이브러리의 동적 드라이버 인터페이스 의존성 기록 |
| Linux VAAPI | 실제 번들에 포함한 libva·libdrm·추가 전이 라이브러리의 MIT/BSD 고지·소스 provenance |
| Windows 런타임 | PE import에서 확인한 UCRT·VC 런타임 배포 조건과 기존 설치 요구 기록 |
| 도구·이미지 | MSVC/SDK 계약, MSYS2·GCC·Make·셸·RPM별 라이선스와 lock. 제품 runtime SBOM과 빌드 환경 SBOM 구분 |

기본 프로파일에서 libaom·dav1d·rav1e·SVT-AV1·vvenc·ass·bluray·placebo·OpenCORE·zmq 등 광범위 외부 구성을 제거한다. FFmpeg 문서는 기본 LGPL2.1+, GPL/version3 활성화 조건과 IJG 고지를 명시한다. 새 빌드의 `avcodec_license()`·`avformat_license()`·`avutil_license()`·`swscale_license()`와 configure·실제 object/SBOM이 모두 승인 프로파일과 맞아야 한다. LGPL2.1 동적 교체·디버깅·소스 제공에 관한 배포 조건도 최종 배포 고지에 반영한다. [FFmpeg 라이선스 원문](https://raw.githubusercontent.com/FFmpeg/FFmpeg/release/8.1/LICENSE.md)

GPL/nonfree 금지는 FFmpeg 산출물의 구성 정책이다. 다운로드 목록에는 GPL인 Make·셸·GCC 등 빌드 도구가 포함되므로 사용 범위와 라이선스를 승인 기록에 명시한다. 런타임에 GPL 전용 object가 유입되거나 version3를 요구하는 의존성이 발견되면 산출물을 수용하지 않는다.

### 5.3 보안·유지보수

등록된 디코더·필터·프로토콜과 외부 코드를 줄이면 연결 가능한 파서 경로와 SBOM 관리 대상이 감소한다. RTSP 의존성으로 선택되는 demuxer와 네트워크·H264/HEVC/JPEG 파서는 계속 점검 대상이다. 버전 고정·CVE 점검·재빌드를 함께 운영한다.

주1회 FFmpeg 8.1 보안 변경과 사용 의존성 공지를 점검하고, 월1회 보안 갱신 후보를 검토하는 운영안을 제안한다. 치명적 취약점은 영향 분석·입력 노출 평가 후 긴급 재빌드·회귀 일정을 잡는다. digest·소스SHA·설정·도구 체인 변경마다 배포 artifact 식별자를 새로 부여한다. 보안 업데이트 판단은 유지보수 담당자와 SLA 확정이 필요하다.

## 6. 교체 영향과 주요 위험

| 영역 | 예상 수정·확인 범위 |
|---|---|
| ABI 이름 | 같은8.1 소스에서는 avcodec62·avformat62·avutil60·swscale9를 유지하는 계획. Windows `avcodec-62.dll` 등과 Linux `libavcodec.so.62` 등 확인. suffix 변경은 별도 통합 변경 |
| Windows import | avcodec.lib·avformat.lib·avutil.lib·swscale.lib를 새 DLL과 함께 생성. MSVC/lib.exe 생성 파일 사용. MinGW 대안에서는 import 형식·export를 검사 |
| Linux 링커 스크립트 | 기존 `INPUT(libavcodec.so.62)` 등4개 패턴 유지. full-version 파일을 SONAME 이름으로 저장하는 현재 UBT 우회 방식 보존 |
| swresample 제거 | `DTCctvFFmpeg.Build.cs`의 Windows 링크·런타임, Linux 링크·런타임 목록과 해당 파일·링커 스크립트 제거. 새 avcodec의 PE/ELF 전이 참조0 확인 |
| Linux VAAPI | 권장 초안은 distro libva/libva-drm/libdrm의 동적 링크. 신규 DT_NEEDED를 전개해 필요한 재배포 가능 MIT/BSD SO를 패키지 옆에 함께 스테이징하고 `$ORIGIN` 검색을 검증. X11·pciaccess 등이 추가되면 실제 의존성에 따라 포함 |
| 진단 코드 | `TestHost/Scripts/inspect_ffmpeg.py`의 swresample 선행 로드 가정을 새 manifest에 맞게 조정. `linux_server_check`와 패키지 도구의 실제 라이브러리 경로 확인 |
| 헤더·고지 | 전체 공용 FFmpeg 헤더를 같은 소스에서 생성·설치. ThirdParty README에 소스·해시·옵션·최소ABI·변경점 갱신. LGPLv2.1/IJG/MIT·source bundle 고지 검증 |
| 배포·복구 | 새 패키지를 별도 경로에서 검증. 기존 BtbN 파일·패키지 해시를 보관하고 manifest 단위로 원복 가능하게 관리. Git LFS 대상과 크기 보고 |

VAAPI를 avutil에 동적 연결하면 NVIDIA만 쓰는 장치도 해당 SO의 로드 의존성을 갖는다. 최소 RHEL8 검증 컨테이너에서 이 의존성이 모두 해결되는지 확인한다. 장기 대안은 PIC로 빌드한 최소 libva/libdrm 정적 포함이며, 소스·빌드 의존성·고지와 추가 보안 패치 책임이 생긴다. 기본안은 distro 패키지의 재배포 대상 SO와 전이 의존성을 manifest로 명시하는 방식이다.

| 위험 | 후속 확인과 처리 |
|---|---|
| D3D11VA 헤더·SDK | MSVC x64 개발 환경과 Windows SDK에서 HEVC/Main10 관련 타입과 함수 검출 확인. PATH에서 MSVC link.exe가 MSYS link.exe보다 우선하는지 확인. CUDA가 없는 조건의 D3D11VA 패키지 실측 |
| Windows 스레드·컴파일러 | 현재 GCC/pthreads와 새 MSVC/w32threads 차이가 있다. CRT 링크 선택과 AVFrame/AVPacket의 FFmpeg 할당·해제 경계도 확인한다. 전체 기능 게이트와 별도 반복 A/B로 decode·p95·CPU를 비교 |
| VAAPI 헤더·드라이버 | libva2.13/RHEL8 헤더로 필요한 H264·HEVC Main10·MJPEG 타입 확인. `/dev/dri/renderD*`·권한·적합 드라이버·실제 Intel/AMD GPU 필요 |
| GLIBC·새 SO | 전체 DT_NEEDED·RPATH/RUNPATH·버전 요구 조사. 공개 GLIBC 요구2.28 이하, GLIBC_PRIVATE 추적, RHEL8 실제 로드·디코딩 확인 |
| NVIDIA API | n13.0.19.0과 운영 드라이버 호환 확인. 소스 configure가 요구하는 헤더 API와 실제 SDK·driver 최소 조건을 lock에 기록 |
| 줄어든 입력 범위 | MPEG4/VP9/AV1·오디오·RTSPS 필요 여부 확인. 추가 요구가 있으면 대응 프로파일·라이선스·크기·테스트 갱신 |
| 성능·기존 미달 | 경량화 후에도 G의 C7 SW·C8과 F 자원 미달을 판정표에 유지. 기존 FAIL과 새 회귀를 구분하며 수용 결정 전 전체 성공으로 표시하지 않음 |
| 장기 운영 | 빌드 담당·보안 공지 점검 담당·배포 고지 담당과 갱신 SLA 지정. 소스·패키지 캐시·lock을 보존 |

## 7. 승인 후 실행 순서·검증·시간

### 7.1 다운로드와 격리 빌드

1. D1 전체 소스 SHA, D2 tag SHA, D3~D14 버전·byte·SHA256·image digest·라이선스·전이 목록을 고정한다. 공급자 해시와 로컬 해시를 대조한다. 총량 상한 초과나 source pin 변경은 승인 목록을 갱신한다.
2. 전용 Windows 도구 디렉터리와 새 RHEL8 빌더를 준비한다. 소스·도구 입력 lock과 SOURCE_DATE_EPOCH·locale·timezone·빌드 명령을 기록한다. 기준 BtbN 원본과 새 설치 prefix를 분리한다.
3. 공통 소스에서 양 플랫폼의 configure 로그·활성 component·라이선스·import/export·SONAME·DT_NEEDED·GLIBC 요구를 수집한다. 승인된 native3·HW 경로와 auto-selected RTSP 의존성을 검수한다.
4. Build.cs·진단 로더·헤더·고지·스테이징 변경을 의미 단위로 구현한다. UE Editor·단위15개·Win64/Linux 패키지를 새 라이브러리로 만든다.
5. 아래 검증을 순서대로 실행한다. 빌드·측정은 한 번에 하나다. 시작 전 공존 검사, 다른 작업은5분 간격 최대30분 대기, 중첩 측정은 무효 처리한다. 종료 시 소유 PID·생성시각·경로와 생성 container ID만 정리한다.

### 7.2 기능·성능 게이트

| 검증 | 수용 기준·범위 |
|---|---|
| 패키지·로더 | 새 라이브러리 해시·소스 provenance, 누락 의존성0, Windows 비검은 PNG·output_w 양수, Linux RHEL8 FFmpeg 로드·전 채널 Playing |
| 단위 | 전체15개 실행, 미수행·실패를 성공으로 표시하지 않음. 실제 형식 없음과 정상 SW 폴백·연결 유지 회귀 포함 |
| 코덱 전체 | G C1~C9×Windows/LinuxSW/LinuxCUDA27조합. 16채널·초기10초 제외80초 이상, 전체/채널별 원본97%, 손실·재연결·오류0, Windows p95≤70ms(30fps)/50ms(60fps), 대체≤1%, PNG 정상 |
| 하드웨어 | Windows CUDA·D3D11VA, Linux CUDA·VAAPI의 실제 backend·Main10/P010 전송. CUDA4/SW12와 fallback16을 구분. VAAPI 장치가 없으면 해당 실행은 ENV_BLOCKED |
| 네트워크 | 기본 TCP와 UDP, RTP/RTCP·인증·긴 GOP 최초 화면·장애 취소/복구. 준비 로그·ffprobe 규격 확인을 유지 |
| F 지연4·보조 | 144/60·60/60·30/60·60/30, 144/30, 버스트0.3초·정지2초를 F/E 기준 그대로 수행 |
| F 정지 | GT3/5/10초·렌더5초의 패킷/재연결/키 대기·복귀2초 이내와 PNG 검수. 강제 정지 대체는 별도 기록 |
| UI·공존·장애 | drag·extremes·subset·disconnected·substream PNG 직접 검수, 서브/메인 전환 CSV, Windows/Linux Pixel Streaming 로드 공존, fault 복구5초 이내 |
| Close/Open100 | 변경된 FFmpeg 자원 수명을 검사. 승인된 F의 모듈별 수·11~40회/마지막30회 최대 비교와 Private·GPU·RHI 기준 유지 |
| 마지막 소크 | 확정한 패키지로 게임60fps·워밍업 제외60분, F/E FPS·지연·대체·손실 기준과 승인된 TID·OS 모듈·총수 비증가, Private/GPU 추세 |

G의 일부 기본 자료는 C++95cd411이다. FFmpeg 교체 효과를 비교할 기준 측정은 **같은 최종 C++9c98d85와 기존 BtbN 라이브러리**로 다시 확보한다. 새 라이브러리와 같은 하네스·입력 파일·HW4·SW 스레드·틱·관측 설정을 사용한다. 27조합의 기존/신규를 직렬 비교하고 C1·C4·C6·C7·C8 대표 조건은 각3회 A/B로 반복해 분포·중앙값·CPU/GPU를 기록한다. 상대 성능 동등의 허용 오차는 사용자와 확정할 항목이며, 절대97% 게이트를 그대로 유지한다.

현재 실제 VAAPI·Linux Vulkan 렌더링의 수용 자료가 부족하다. 장치·서버 범위를 정해 환경 판정을 해소해야 H 목표의 전체 HW 동등성을 승인할 수 있다. G/F의 기존 FAIL과 새 패키지의 실패를 모두 최종 표에 남긴다.

### 7.3 시간 예산

| 작업 | 예상 시간 |
|---|---:|
| lock·다운로드·두 빌드 환경 준비 | 30~90분 |
| Windows/Linux FFmpeg 순차 빌드·정적 라이선스/ABI 검사 | 30~60분 |
| UE Editor·전체 단위·Win64/Linux 패키징 | 15~45분 |
| 기준/신규 코덱27조합씩54회·PNG·HW 확인 | 110~150분 |
| 대표5조건 추가 A/B 반복20회 | 35~50분 |
| F 지연·정지·UI·공존·장애·Close/Open100 | 65~100분 |
| 마지막60분 소크·자원 분석 | 65~75분 |
| 최종 보고·고지·배포 manifest·정리 | 15~30분 |

합계는 **약6~10시간의 순차 작업**이며 첫 환경 구성 실패·실서버 이동·다른 세션 대기는 별도다. G의 기본 코덱 케이스가 약100초, F100회 자료가 약1210초였던 로컬 기록을 시간 예산에 반영했다. 실측 성능·용량은 후속 빌드와 검증에서 확정한다.

## 8. 결정할 항목

1. 권장 직접 빌드 방식과 D1~D14·최대0.8GiB 다운로드 예산, 새 빌더 생성·격리 시험 범위 승인.
2. 기본 영상3코덱·RTSP TCP/UDP 범위 확정. 운영의 MPEG4/VP9/AV1·RTSPS 요구가 있으면 호환 프로파일 선택.
3. 운영 NVIDIA driver 최소 버전, VAAPI 검증용 Intel/AMD 장치와 Linux GPU 서버 범위.
4. 반복 A/B의 상대 성능 동등 허용 범위, G/F의 기존 실패 수용·후속 개선 범위, 장기 보안 갱신 담당.

실제 동봉 교체는 격리 시험 결과와 위 수용 범위를 검수한 후 별도 승인 단계로 둔다.
