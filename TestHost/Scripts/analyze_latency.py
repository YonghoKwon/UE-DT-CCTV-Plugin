#!/usr/bin/env python3
"""CCTV-01E CSV의 워밍업 제외 지연·FPS·프레임 대체를 재현 가능하게 판정한다."""
import argparse
import csv
import json
import math
from pathlib import Path


def load_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def histogram(text):
    return {int(k): int(v) for item in text.split(";") if item for k, v in [item.split(":")]}


def age_summary(row):
    hist = histogram(row["frame_age_hist"])
    count = int(row["frame_age_samples"])
    if sum(hist.values()) != count:
        raise ValueError("지연 히스토그램과 전체 표본 수 불일치")
    target = math.ceil(count * 0.95)
    cumulative = 0
    p95 = 0.0
    for key, value in sorted(hist.items()):
        cumulative += value
        if cumulative >= target:
            p95 = float(row["frame_age_max_ms"]) if key == 1001 else key
            break
    return dict(samples=count, mean_ms=float(row["frame_age_total_ms"])/count if count else 0,
                p95_ms=p95, max_ms=float(row["frame_age_max_ms"]), latest_ms=float(row["frame_age_latest_ms"]))


def analyze(folder, game_fps, video_fps, recovery=False):
    rows = load_csv(folder/"samples.csv")
    steady = [r for r in rows if float(r["t"]) >= 10]
    first, last = steady[0], steady[-1]
    seconds = float(last["t"])-float(first["t"])
    if seconds <= 0:
        raise ValueError("측정 구간 없음")
    decoded = int(last["decoded"])-int(first["decoded"])
    displayed = int(last["displayed"])-int(first["displayed"])
    superseded = int(last["superseded"])-int(first["superseded"])
    channel_rows = load_csv(folder/"samples.channels.csv")
    stamp = max(float(r["t"]) for r in channel_rows)
    latest = [r for r in channel_rows if float(r["t"]) == stamp]
    channels = [dict(channel=int(r["channel"]), **age_summary(r)) for r in latest]
    summary = dict(status="FAIL", game_fps_limit=game_fps, video_fps=video_fps,
                   seconds=seconds, first_t=float(first["t"]), last_t=float(last["t"]),
                   decode_fps=decoded/(16*seconds), display_fps=displayed/(16*seconds),
                   actual_game_fps_mean=sum(float(r["frame_fps"]) for r in steady)/len(steady),
                   superseded=superseded, superseded_all=max(int(r["superseded"]) for r in rows),
                   replacement_rate=superseded/decoded if decoded else 1,
                   dropped=max(int(r["dropped"]) for r in rows),
                   reconnects=max(int(r["reconnects"]) for r in rows),
                   errors=max(int(r["errors"]) for r in rows),
                   peak_queued_frames=max(int(r["peak_queued_frames"]) for r in rows),
                   last_queued_frames=int(last["queued_frames"]),
                   all_playing=all(int(r["playing"]) == 16 for r in steady),
                   age=age_summary(last), channels=channels,
                   worst_channel_p95_ms=max(r["p95_ms"] for r in channels), failures=[],
                   method="기존 누적 프레임 차이로 FPS·대체율 계산. 지연은 하네스10초 리셋 뒤 제출한 모든 프레임의1ms 상한 히스토그램. 채널별 최악p95로도 기준을 확인.")
    failures = summary["failures"]
    if seconds < 80 or len(latest) != 16 or not summary["all_playing"] or any(r["samples"] == 0 for r in channels):
        failures.append("80초·전16Playing·지연 표본 기준 미달")
    if summary["dropped"] or summary["reconnects"] or summary["errors"]:
        failures.append("패킷 손실·재연결·오류0 기준 미달")
    limits = {(144, 60): (58, 34, 0), (60, 60): (58, 50, 0.01),
              (30, 60): (28, 100, None), (60, 30): (29, 50, 0), (144, 30): (29, 50, 0)}
    display_limit, age_limit, replacement_limit = limits[(game_fps, video_fps)]
    if summary["display_fps"] < display_limit:
        failures.append("표시 FPS 기준 미달")
    if (game_fps, video_fps) == (60, 60) and summary["decode_fps"] < 58:
        failures.append("디코딩58fps 기준 미달")
    if summary["worst_channel_p95_ms"] > age_limit:
        failures.append("채널별 frame age p95 기준 미달")
    if replacement_limit is not None and summary["replacement_rate"] > replacement_limit:
        failures.append("프레임 대체 기준 미달")
    if recovery:
        events = load_csv(folder/"samples.events.csv")
        end = next(float(r["t"]) for r in events if r["event"] == "stall-end")
        by_time = {}
        for row in channel_rows:
            if float(row["t"]) >= end:
                by_time.setdefault(float(row["t"]), []).append(row)
        recovered = None
        ordered = sorted(by_time.items())
        baseline = {int(r["channel"]):int(r["frame_age_samples"]) for r in ordered[0][1]}
        for stamp, group in ordered[1:]:
            if len(group) == 16 and all(int(r["frame_age_samples"]) > baseline[int(r["channel"])] and float(r["frame_age_latest_ms"]) <= age_limit for r in group):
                recovered = stamp-end
                break
        summary["recovery"] = dict(stall_end_t=end, observed_upper_seconds=recovered,
                                   threshold_ms=age_limit, sample_interval_seconds=1,
                                   method="정지 직후 첫 CSV를 기준으로 전16채널 제출 표본 수가 증가하고 마지막 제출 age가 기준 이하인 첫 후속CSV 시각. 정지 전 값의 재사용을 막는 보수적 관측 상한.")
        if recovered is None or recovered > 2:
            failures.append("정지 후2초 이내 지연 회복 기준 미달")
        # 강제 정지 자체의 프레임 대체는 별도 수치로 보존한다.
        failures[:] = [x for x in failures if x != "프레임 대체 기준 미달"]
    summary["status"] = "FAIL" if failures else "PASS"
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--game-fps", type=int, required=True)
    parser.add_argument("--video-fps", type=int, default=60)
    parser.add_argument("--recovery", action="store_true")
    args = parser.parse_args()
    value = analyze(args.folder, args.game_fps, args.video_fps, args.recovery)
    (args.folder/"latency-analysis.json").write_text(json.dumps(value, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in value.items() if k not in ("channels", "method")}, ensure_ascii=False, indent=2))
