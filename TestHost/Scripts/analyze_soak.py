#!/usr/bin/env python3
"""게임60fps 소크와 사용자가 승인한 스레드 집합·총수 비증가 기준을 판정한다."""
import argparse
import csv
import datetime as dt
import json
import re
from pathlib import Path
from analyze_latency import analyze as analyze_latency


OS_MODULES = {"ntdll.dll", "kernel32.dll", "kernelbase.dll", "mswsock.dll", "ws2_32.dll", "crypt32.dll",
              "advapi32.dll", "rpcrt4.dll", "sechost.dll", "bcrypt.dll", "bcryptprimitives.dll", "sspicli.dll",
              "ncrypt.dll", "combase.dll", "user32.dll", "win32u.dll", "gdi32.dll", "ole32.dll", "shell32.dll"}


def trend(rows, key, scale):
    points = [(r["t"], float(r[key])/scale) for r in rows if r.get(key) not in (None, "")]
    if len(points) < 2:
        return {"status":"NOT_RUN", "samples":len(points)}
    xs, ys = zip(*points)
    xm, ym = sum(xs)/len(xs), sum(ys)/len(ys)
    slope = sum((x-xm)*(y-ym) for x,y in points)/sum((x-xm)**2 for x in xs)*3600
    return dict(status="PASS", samples=len(points), first=ys[0], last=ys[-1], min=min(ys), max=max(ys),
                delta=ys[-1]-ys[0], slope_per_hour=slope,
                note="추세 계측 완료 상태이며 누수 없음의 판정은 아님.")


def analyze(folder, log, modules):
    latency = analyze_latency(folder, 60, 60)
    failures = list(latency["failures"])
    if latency["seconds"] < 3600:
        failures.append("워밍업 제외60분 기준 미달")
    text = log.read_text(encoding="utf-8-sig", errors="replace")
    stamp = re.search(r"\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start", text).group(1)
    origin = dt.datetime.strptime(stamp, "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=dt.timezone.utc)
    last_t = latency["last_t"]
    resources = []
    with (folder/"resources.csv").open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            row["t"] = (dt.datetime.fromisoformat(row["utc"].replace("Z", "+00:00"))-origin).total_seconds()
            if 600 <= row["t"] <= last_t:
                resources.append(row)
    snapshots = []
    with modules.open(encoding="utf-8-sig") as stream:
        for line in stream:
            sample = json.loads(line)
            sample["t"] = (dt.datetime.fromisoformat(sample["utc"])-origin).total_seconds()
            if 600 <= sample["t"] <= last_t:
                snapshots.append(sample)
    if not resources or not snapshots:
        failures.append("10분 이후 자원·모듈 표본 없음")
        return dict(status="FAIL", failures=failures, latency=latency)
    module_names = sorted({m for sample in snapshots for m in sample["counts"]})
    stable, changing = {}, []
    for module in module_names:
        first_set = {t["tid"] for t in snapshots[0]["threads"] if t["module"] == module}
        same = True
        removed, added = set(), set()
        for sample in snapshots[1:]:
            current = {t["tid"] for t in sample["threads"] if t["module"] == module}
            same &= current == first_set
            removed.update(first_set-current)
            added.update(current-first_set)
        stable[module] = dict(fixed=bool(same), os_module=module in OS_MODULES,
                              initial_tids=sorted(first_set), added_tids=sorted(added), removed_tids=sorted(removed),
                              count_min=min(r["counts"].get(module,0) for r in snapshots),
                              count_max=max(r["counts"].get(module,0) for r in snapshots))
        if not same:
            changing.append(module)
    unknown = sum(sum(t["query_status"] != 0 or t["module"] == "(unknown)" for t in r["threads"]) for r in snapshots)
    required = [m for m in module_names if m == "dtcctvtesthost.exe" or m == "ucrtbase.dll" or m.startswith("nv")]
    required_fixed = bool(required) and "dtcctvtesthost.exe" in required and "ucrtbase.dll" in required and all(stable[m]["fixed"] for m in required)
    only_os_varies = all(m in OS_MODULES for m in changing)
    first_window = [r for r in snapshots if 600 <= r["t"] < 1200]
    last_window = [r for r in snapshots if last_t-600 <= r["t"] <= last_t]
    first_max = max(r["total"] for r in first_window) if first_window else None
    last_max = max(r["total"] for r in last_window) if last_window else None
    totals_not_growing = first_max is not None and last_max is not None and last_max <= first_max
    resource_first = [int(r["threads"]) for r in resources if r["t"] < 1200]
    resource_last = [int(r["threads"]) for r in resources if r["t"] >= last_t-600]
    resource_not_growing = bool(resource_first and resource_last) and max(resource_last) <= max(resource_first)
    if not required_fixed:
        failures.append("실행파일·CRT·NVIDIA TID 집합 일정 기준 미달")
    if not only_os_varies or unknown:
        failures.append("변동 OS 모듈 한정 또는 시작 주소 조회 기준 미달")
    if not totals_not_growing or not resource_not_growing:
        failures.append("마지막10분 총수 최대가10~20분 최대를 넘음 또는 표본 부족")
    gaps = [b["t"]-a["t"] for a,b in zip(snapshots,snapshots[1:])]
    periodic_path = modules.with_suffix(".5min.jsonl")
    periodic = [json.loads(line) for line in periodic_path.read_text(encoding="utf-8-sig").splitlines()] if periodic_path.exists() else []
    if len(periodic) < 12:
        failures.append("5분 모듈 집계 표본 부족")
    minimum, maximum = min(r["total"] for r in snapshots), max(r["total"] for r in snapshots)
    blocks = {}
    for start in range(600, int(last_t), 300):
        block = [r for r in resources if start <= r["t"] < start+300]
        if block:
            blocks[str(start)] = trend(block, "private_bytes", 1048576)
    result = dict(status="FAIL" if failures else "PASS", failures=failures, latency=latency,
                  resource_window=dict(start=resources[0]["t"], end=resources[-1]["t"], samples=len(resources)),
                  private_memory_mib=trend(resources, "private_bytes", 1048576),
                  working_set_mib=trend(resources, "working_set_bytes", 1048576),
                  gpu_dedicated_mib=trend(resources, "gpu_dedicated_bytes", 1048576),
                  gpu_shared_mib=trend(resources, "gpu_shared_bytes", 1048576),
                  private_five_minute_blocks=blocks,
                  thread_criteria=dict(status="PASS" if required_fixed and only_os_varies and totals_not_growing and resource_not_growing and not unknown else "FAIL",
                    source="docs/agent/CCTV-01E.md 사용자 결정(2026-11-06)", required_tid_sets_fixed=required_fixed,
                    only_os_modules_vary=only_os_varies, changing_modules=changing, unknown_queries=unknown,
                    total_min=minimum, total_max=maximum, old_total_constant_status="PASS" if minimum == maximum else "FAIL",
                    first_10_to_20_max=first_max, final_ten_max=last_max,
                    resources_first_10_to_20_max=max(resource_first), resources_final_ten_max=max(resource_last),
                    raw_snapshot_count=len(snapshots), maximum_raw_gap_seconds=max(gaps), periodic_snapshot_count=len(periodic),
                    module_sets=stable),
                  known_limits=["frame age는 디코더 반환~렌더 등록이며 GPU 완료·실제 모니터 표시와 네트워크 지연은 제외.",
                                "스레드 시작 주소는1초 표본, 모듈 집계는5분 표본. 두 조회 사이에 생성·소멸한 스레드는 입증하지 못함.",
                                "메모리 추세는 자원 관측이며 할당 수준의 누수 없음 입증과 구분. 처음10분 이후 종료 요청 전까지만 분석."])
    if result["private_memory_mib"]["status"] != "PASS" or result["gpu_dedicated_mib"]["status"] != "PASS":
        failures.append("Private·GPU 메모리 추세 표본 부족")
        result["status"] = "FAIL"
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    args = parser.parse_args()
    value = analyze(args.folder, args.log, args.modules)
    (args.folder/"soak-analysis.json").write_text(json.dumps(value, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"status":value["status"],"failures":value["failures"],"private_memory_mib":value.get("private_memory_mib"),
                      "thread_criteria":{k:v for k,v in value.get("thread_criteria",{}).items() if k != "module_sets"}},ensure_ascii=False,indent=2))
