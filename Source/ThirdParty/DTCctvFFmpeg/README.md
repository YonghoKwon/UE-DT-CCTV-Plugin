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
- DTCctv는 FFmpeg 함수를 직접 import하지 않습니다. 수정 전 Win64·Linux 오브젝트의 미해결 심볼 합집합 52개를 실행 중 함수 표로 해석합니다. Windows는 전체 경로 `LoadLibraryExW`·`GetProcAddress`, Linux는 전체 경로 `dlopen(RTLD_NOW | RTLD_LOCAL)`·`dlsym`을 사용합니다. 바이너리·SONAME·스테이징 이름은 그대로입니다.
- 로드 순서는 avutil → swresample → swscale → avcodec → avformat입니다. 에디터는 DTCctv 모듈 Binaries, 패키지는 실행 파일 옆의 파일 5개를 사용합니다. 하나라도 파일·함수가 빠지면 부분 표를 게시하지 않고 실패 원인을 로그·스트림 오류로 전달합니다.
- Windows import `.lib` 5개와 Linux `libdtcctv_*.so` 링커 스크립트 5개는 더 이상 링크 입력이 아니므로 제거했습니다. DLL/SO 5개와 헤더·라이선스는 유지합니다. 정적 import 제거와 실제 결합 검증 범위는 [공존 검증 기록](../../../docs/FFMPEG_MEDIA_COEXISTENCE.md)을 따릅니다.
- 바이너리는 Git LFS로 관리합니다. 클론 후 `git lfs pull`이 필요할 수 있습니다.
- 종료 시 세션·렌더 명령·HW 참조를 정리한 뒤 라이브러리를 역순 해제합니다. 남은 세션·프레임 소유자가 있으면 해제를 보류하고 로그에 근거를 남깁니다. 동시 재생 수용 여부는 위 검증 기록의 최신 결과를 확인하십시오.

현재 런타임 파일 목록(각 플랫폼 5개):

| Windows `Win64/bin/` | Linux `Linux/lib/` |
|---|---|
| `avutil-60.dll` | `libavutil.so.60` |
| `swresample-6.dll` | `libswresample.so.6` |
| `swscale-9.dll` | `libswscale.so.9` |
| `avcodec-62.dll` | `libavcodec.so.62` |
| `avformat-62.dll` | `libavformat.so.62` |

CCTV-01Y에서 단독·공존 패키지와 Editor 모듈의 신 계열 import/DT_NEEDED 및 DTCctv 오브젝트의 FFmpeg 미해결 심볼은 0이었습니다. 실행 주소·Windows IAT·Linux LD_DEBUG/maps로 구 호출은 구 계열, 새 함수 표와 새 라이브러리 내부 호출은 새 계열임을 확인했습니다. `RTLD_DEEPBIND`는 사용하지 않았습니다. 파일 누락 실제 실행은 크래시 없이 명시적 오류로 종료했고, 정상 시험 종료는 소유자 0에서 역순 해제를 확인했습니다. 소유자가 남아 해제를 보류하는 경로의 실제 장시간 종료 시험은 별도 미검증입니다. 전체 01Y 판정에는 Y6 실패가 있으므로 공존 결과와 구분합니다.
