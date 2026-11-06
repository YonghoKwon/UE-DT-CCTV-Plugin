# [Claude → Codex] CCTV-01H FFmpeg 경량 빌드 검토

사용자가 검토를 승인했다. 이번 단계는 **검토와 계획까지**다. 다운로드, 빌드, 동봉 바이너리 교체는 하지 않고, 필요한 다운로드와 빌드 절차를 목록으로 만들어 사용자 승인을 받는다. [CCTV-01C](CCTV-01C.md)의 절대 금지와 공존 규칙을 그대로 적용한다.

## 배경

동봉 FFmpeg(BtbN `lgpl-shared` n8.1)는 `--enable-version3`로 LGPL v3이고, 플러그인이 쓰지 않는 외부 라이브러리 40여 개(libaom, librav1e, libsvtav1, libvvenc, libass, libbluray, libplacebo, libopencore-amr, libzmq 등)가 들어 있다. Git LFS 바이너리는 약 257MB다.

## 목표

- 필요한 기능만 넣는다: RTSP, RTP, TCP, UDP 네트워크, H.264, H.265, MJPEG 디코더(CCTV-01G 결과에서 필요가 확인된 코덱 포함), swscale, 하드웨어 가속(CUDA/NVDEC, D3D11VA, VAAPI).
- `--enable-version3` 없이 LGPL v2.1 이상으로 만든다. GPL과 nonfree는 쓰지 않는다.
- Windows x64와 Linux x64(glibc 2.28 이하, RHEL 8)를 지원한다.
- 기능, 성능, 하드웨어 디코딩이 지금 빌드와 같아야 한다.

## 검토 항목

1. **현재 사용 기능 목록:** 플러그인 코드와 CCTV-01G 결과에서 실제로 쓰는 FFmpeg 기능(디먹서, 프로토콜, 디코더, hwaccel, swscale 형식)을 정리한다.
2. **빌드 방식 비교:**
   - BtbN FFmpeg-Builds의 사용자 정의 변형
   - 직접 configure: Windows는 MSYS2/MinGW 또는 교차 컴파일, Linux는 RHEL 8 계열 컨테이너
   - 기타
   각각에 필요한 다운로드(소스, 도구 체인, 이미지와 크기, 출처 URL), 재현성, glibc 2.28 호환, 유지보수 부담을 표로 비교한다.
3. **configure 초안:** `--disable-everything`을 기본으로 하고 필요한 항목만 켜는 옵션을 작성한다. NVDEC에 필요한 `nv-codec-headers`(MIT)처럼 추가 의존성과 라이선스를 적는다.
4. **예상 효과:** 크기 추정(가능하면 근거 포함), 라이선스 고지 목록 변화, 보안 노출 면적 변화.
5. **교체 영향:** SONAME과 DLL 이름 변화, Build.cs와 링커 스크립트 수정 범위, 다시 해야 할 검증(빌드, 단위 테스트, 패키징, 지연 4조건, 정지 시험, 코덱 전체, Linux, 60분 soak)과 예상 시간.
6. **위험:** VAAPI와 D3D11VA 빌드 의존성, 새 빌드의 장기 유지보수(보안 업데이트 주기) 등.

## 산출물

- 의미 단위 커밋(`CCTV-01H`): 이 지시서와 검토 문서 `docs/FFMPEG-BUILD.md`. 코드와 바이너리는 바꾸지 않는다. push, PR, merge는 금지다.
- `Saved/Reports/CCTV-01H/RESULT.md`: 권장안 하나와 대안, 사용자 승인이 필요한 다운로드 목록(URL, 크기, 라이선스).

완료 보고: 권장안 / 다운로드 목록 / 예상 효과 / 교체 영향과 시간 / 결정 필요.
