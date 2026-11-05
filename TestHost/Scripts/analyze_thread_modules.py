#!/usr/bin/env python3
"""시작 주소 모듈 스냅샷을 구간 끝 자원 또는 장시간 측정과 대조한다."""
import argparse
import datetime as dt
import json
import re
from collections import Counter
from pathlib import Path


def analyze(raw, game_log, endpoints=None, tail_cycles=10):
    text = game_log.read_text(encoding="utf-8-sig", errors="replace")
    stamp = re.search(r"\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start", text).group(1)
    origin = dt.datetime.strptime(stamp, "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=dt.timezone.utc)
    samples = [json.loads(line) for line in raw.read_text(encoding="utf-8-sig").splitlines()]
    for sample in samples:
        sample["t"] = (dt.datetime.fromisoformat(sample["utc"])-origin).total_seconds()
    selected = []
    if endpoints:
        for endpoint in endpoints:
            sample = [r for r in samples if r["t"] <= endpoint["t"]][-1]
            selected.append(dict(cycle=endpoint["cycle"], resource_t=endpoint["t"], snapshot_t=sample["t"],
                                 offset_seconds=sample["t"]-endpoint["t"], counts=sample["counts"], threads=sample["threads"]))
        window = selected[-tail_cycles:]
    else:
        window = [r for r in samples if 600 <= r["t"] <= 3600]
    modules = sorted({m for sample in window for m in sample["counts"]})
    ranges = {m:[min(r["counts"].get(m,0) for r in window), max(r["counts"].get(m,0) for r in window)] for m in modules}
    fixed = {}
    for module in modules:
        sets = [{t["tid"] for t in r["threads"] if t["module"] == module} for r in window]
        fixed[module] = dict(fixed=all(s == sets[0] for s in sets), first_count=len(sets[0]), last_count=len(sets[-1]))
    added, removed = Counter(), Counter()
    for sample in samples[1:]:
        added.update(t["module"] for t in sample["added"])
        removed.update(t["module"] for t in sample["removed"])
    unknown = sum(sum(t["query_status"] != 0 or t["module"] == "(unknown)" for t in sample["threads"]) for sample in window)
    result = dict(status="PASS" if samples and window and not unknown else "FAIL", snapshots=len(samples), selected=len(window),
                  first_t=samples[0]["t"], last_t=samples[-1]["t"],
                  interval_min_seconds=min(b["t"]-a["t"] for a,b in zip(samples,samples[1:])),
                  interval_max_seconds=max(b["t"]-a["t"] for a,b in zip(samples,samples[1:])),
                  module_ranges=ranges, stable_tid_sets=fixed, unknown_or_failed_queries=unknown,
                  added_modules=dict(added), removed_modules=dict(removed),
                  selected_endpoints=[{k:v for k,v in r.items() if k != "threads"} for r in selected],
                  note="스냅샷 시각의 시작 주소 소속만 판정. 수집 사이에 생성·소멸한 스레드는 관측하지 못함. Close/Open에서는 세션 스레드TID 교체가 정상이며 재생 구간 끝 모듈 수를 비교. 총수 일정 게이트와 별도 관측 상태.")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("--game-log", type=Path, required=True)
    parser.add_argument("--endpoints", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument('--tail-cycles',type=int,default=10)
    args = parser.parse_args()
    endpoints = json.loads(args.endpoints.read_text(encoding="utf-8-sig"))["endpoints"] if args.endpoints else None
    result = analyze(args.raw, args.game_log, endpoints, args.tail_cycles)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k not in ("stable_tid_sets", "selected_endpoints", "added_modules", "removed_modules", "note")}, ensure_ascii=False, indent=2))
