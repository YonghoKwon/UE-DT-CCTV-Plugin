# FFmpeg (LGPL shared)

- 버전: n8.1.3-34-ga35157abb9 (2026-11-04)
- 출처: https://github.com/BtbN/FFmpeg-Builds , 릴리스 autobuild-2026-11-04-12-51
- 원본: `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`
  (SHA256 05cb36f8a9fcd30b35cfcbbd6701cb14f5d21ce18efd9c0c60d4688e569bd7d2)
- 원본: `ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz`
  (SHA256 016353104e38df3bc512d229671a3587f8d2986c6f07741b20c6b425c3915a9e)
- 라이선스: LGPL v3 (`LICENSE.txt`), 동적 링크로만 사용합니다. Linux 최소 glibc 2.28.
- 사용하는 라이브러리: avcodec, avformat, avutil, swscale, swresample
- Linux 파일은 심볼릭 링크 없이 SONAME 이름(`libavcodec.so.62` 등)으로 저장합니다.
- `libdtcctv_avcodec.so` 등 `libdtcctv_*.so`는 `INPUT(SONAME)` 텍스트 링커 스크립트입니다. UE 5.3 UBT가 `-ldtcctv_avcodec` 등 고유 링크 이름으로 변환하게 하여 기존 플러그인의 `-lavcodec` 검색과 분리합니다. 일반 이름의 `libavcodec.so` 등은 이 디렉터리에 두지 않습니다. 바이너리 버전과 SONAME은 바꾸지 않으며 실행 시 SONAME 파일 5개를 게임 실행 파일 옆에 스테이징합니다.
- 바이너리는 Git LFS로 관리합니다. 클론 후 `git lfs pull`이 필요할 수 있습니다.
- 고유 링크 이름은 라이브러리 검색만 분리합니다. CCTV-01V에서 monolithic 실행 파일의 공통 C 함수 import는 신·구 간에 합쳐져 동시 재생이 실패했습니다. 실행 ABI 분리 완료를 뜻하지 않으며 [공존 검증 기록](../../../docs/FFMPEG_MEDIA_COEXISTENCE.md)을 확인해야 합니다.
