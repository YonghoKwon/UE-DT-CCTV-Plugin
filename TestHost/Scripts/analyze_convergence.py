#!/usr/bin/env python3
"""30회 Close/Open의 재생 구간 끝 자원과 마지막10회 수렴을 판정한다."""
import argparse
import csv
import datetime as dt
import json
import re
from pathlib import Path


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def slope(points, key):
    xs = [r["cycle"] for r in points]
    ys = [r[key] for r in points]
    xm, ym = sum(xs)/len(xs), sum(ys)/len(ys)
    return sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/sum((x-xm)**2 for x in xs)


def analyze(folder, log, interval=10):
    events = rows(folder/"samples.events.csv")
    samples = rows(folder/"samples.csv")
    opens = [r for r in events if r["event"] == "open"]
    text = log.read_text(encoding="utf-8-sig", errors="replace")
    stamp = re.search(r"\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start", text).group(1)
    anchor = dt.datetime.strptime(stamp, "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=dt.timezone.utc)
    resources = []
    for row in rows(folder/"resources.csv"):
        if not all(row.get(k) for k in ("private_bytes", "handles", "threads", "gpu_dedicated_bytes")):
            continue
        t = (dt.datetime.fromisoformat(row["utc"].replace("Z", "+00:00"))-anchor).total_seconds()
        resources.append(dict(t=t, private_mib=int(row["private_bytes"])/1048576,
                              handles=int(row["handles"]), threads=int(row["threads"]),
                              gpu_mib=int(row["gpu_dedicated_bytes"])/1048576))
    endpoints, missing = [], []
    for event in opens:
        start = float(event["t"])
        end = next((float(r["t"]) for r in events if r["event"] == "close" and float(r["t"]) > start), start+interval)
        near = [r for r in resources if end-2.5 <= r["t"] < end]
        if not near:
            missing.append(int(event["cycle"]))
            continue
        point = near[-1].copy()
        point.update(cycle=int(event["cycle"]), segment_start=start, segment_end=end, sample_before_end_seconds=end-point["t"])
        # 닫기·연결 중 표본이 끝점으로 섞이지 않았는지 가장 가까운 CSV로 확인한다.
        playing = min(samples, key=lambda r: abs(float(r["t"])-point["t"]))
        point["playing"] = int(playing["playing"])
        endpoints.append(point)
    last = endpoints[-10:]
    summary = dict(status="FAIL", requested_cycles=30, observed_opens=len(opens), endpoints=endpoints,
                   last_ten=last, missing_endpoint_cycles=missing, failures=[],
                   method="각 Open 재생 구간 끝 직전2.5초 안의 가장 가까운 자원 표본. 최종 Open도 동일10초 재생 구간으로 비교. 마지막10회 OLS는 회당 기울기. 반올림 허용치를 넣지 않음.")
    if len(opens) != 30 or len(last) != 10 or missing or any(r["playing"] != 16 for r in endpoints):
        summary["failures"].append("30회·16Playing·구간 끝 자원 표본 기준 미달")
    if len(last) == 10:
        summary["metrics"] = {}
        for key in ("private_mib", "handles", "threads", "gpu_mib"):
            metric = dict(first=last[0][key], last=last[-1][key], min=min(r[key] for r in last), max=max(r[key] for r in last),
                          delta=last[-1][key]-last[0][key], slope_per_cycle=slope(last,key))
            metric["status"] = "PASS" if (metric["slope_per_cycle"] <= 1 if key == "private_mib" else metric["delta"] <= 0 and metric["slope_per_cycle"] <= 0) else "FAIL"
            summary["metrics"][key] = metric
            if metric["status"] == "FAIL":
                summary["failures"].append(key+" 증가 기준 미달")
    summary["status"] = "FAIL" if summary["failures"] else "PASS"
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.folder, args.log)
    (args.folder/"convergence-analysis.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    with (args.folder/"cycle-endpoints.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, list(result["endpoints"][0]))
        writer.writeheader()
        writer.writerows(result["endpoints"])
    print(json.dumps({k:v for k,v in result.items() if k not in ("endpoints", "last_ten")}, ensure_ascii=False, indent=2))
