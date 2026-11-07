# [Claude → Codex] CCTV-01Z 최종 정리

사용자 요청: 모든 작업이 끝난 뒤 필요 없는 파일을 반드시 정리한다. CCTV-01L 최종 테스트, 외부 주소 확인, Claude의 최종 검수와 push가 끝난 뒤 마지막으로 실행한다. 다른 세션의 작업 파일과 운영 프로젝트는 절대 건드리지 않는다.

## 원칙

- 삭제 전에 대상 목록(경로, 크기, 이유)을 `Saved/Reports/CCTV-01Z/cleanup-plan.md`로 만들고, 그 목록에 있는 것만 지운다.
- 이 작업이 만든 것만 지운다. 소유가 확인되지 않는 파일, 컨테이너, 이미지는 지우지 않고 목록에 "확인 필요"로 남긴다.
- 휴지통을 거치지 않는 영구 삭제는 아래 "삭제" 목록에 한해서만 한다.
- Git에 커밋된 파일은 지우지 않는다. 저장소 작업 트리는 정리 후 `git status`가 깨끗해야 한다.

## 보존 (지우지 않음)

- 저장소의 커밋된 모든 파일, `docs/agent/` 지시서 전체.
- 각 단계(`Saved/Reports/CCTV-01C`~`CCTV-01Z`)의 `RESULT.md`, `result.json`, `progress.md`, 요약 CSV와 분석 JSON, 판정 근거로 인용된 대표 PNG(단계당 최대 10장 정도).
- 운영 프로젝트(`C:\Unreal Projects\m7at10_dt`, `ma0t10_dt`)와 그 안의 모든 파일.
- 기존 Docker 이미지와 기존 컨테이너, 다른 세션이 만든 파일.
- `C:\Users\ho270\AppData\Local\Temp\cctv\c`(기존 플러그인 원본 clone)와 `ffm-main`은 사용자에게 물어볼 목록에 넣고 지우지 않는다.

## 삭제

| 대상 | 예 |
|---|---|
| 측정 원시 자료 | `Saved/Reports/*/measurements/` 아래의 대용량 원시 CSV, 연속 PNG, 프로세스 로그(요약에 반영된 것) |
| UE 로그 | `Saved/Logs/CCTV-*` |
| 빌드 산출물 | `TestHost/Binaries`, `TestHost/Intermediate`, `TestHost/Saved`(Config 제외), `TestHost/DerivedDataCache`, 플러그인 `Binaries`, `Intermediate` |
| 임시 패키지 | `C:\Users\ho270\AppData\Local\Temp\cctv\pkg*`, `run`, `bin` |
| 시험 스트림과 다운로드 | `Temp\cctv\dl` 아래의 시험 영상 파일, `rpm-tc` RPM. mediamtx와 ffmpeg 도구는 사용자에게 물어볼 목록에 넣는다 |
| 격리 복사본 | `Saved/Reports/CCTV-01J/quarantine` 등 기존 플러그인 복사본, 01I 시험 복사본 |
| Codex 로그 | `Temp\cctv\codex` 아래의 프롬프트와 stderr. `events.jsonl`은 압축해 보존 여부를 사용자에게 묻는다 |
| 이 작업이 만든 컨테이너 | 기록된 컨테이너 ID 중 남은 것 |
| 임시 프로젝트 | DTCore 시험용 임시 프로젝트, 대조 프로젝트 |

## 결과

- 정리 전후 디스크 사용량(C 드라이브 여유, 저장소 폴더, `Temp\cctv`)을 기록한다.
- 지운 목록과 남긴 목록(이유 포함), 사용자에게 물어볼 목록을 `Saved/Reports/CCTV-01Z/RESULT.md`에 쓴다.
- 커밋은 이 지시서와 `.gitignore` 보완이 필요할 때만 한다(`CCTV-01Z`). push, PR, merge는 금지다.

완료 보고: 정리 전후 용량 / 삭제 목록 / 보존 목록 / 사용자 확인 필요 목록.
