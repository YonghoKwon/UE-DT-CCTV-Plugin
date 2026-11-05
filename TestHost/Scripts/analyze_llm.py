#!/usr/bin/env python3
"""UE LLM 행 번호 로그로 CSV 시각을 복원해 재생 구간 끝 할당 태그를 비교한다."""
import argparse
import csv
import datetime as dt
import json
import re
from pathlib import Path


def analyze(folder, game_log, convergence):
    text = game_log.read_text(encoding='utf-8-sig', errors='replace')
    origin_text = re.search(r'\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start', text).group(1)
    def stamp(value):
        return dt.datetime.strptime(value, '%Y.%m.%d-%H.%M.%S:%f').replace(tzinfo=dt.timezone.utc)
    origin = stamp(origin_text)
    times = {}
    for match in re.finditer(r'\[([^\]]+)\].*LogHAL: Wrote LLM csv line (\d+)', text):
        number = int(match.group(2))
        times.setdefault(number, []).append((stamp(match.group(1))-origin).total_seconds())
    results = []
    failures = []
    for path in sorted(folder.glob('LLM*.csv')):
        with path.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.reader(stream)
            header = [s.strip() for s in next(reader)]
            rows = []
            for number, values in enumerate(reader, 1):
                if number not in times or not values:
                    continue
                row = dict(number=number, t=max(times[number]), values={
                    name: float(values[index]) if index < len(values) and values[index].strip() else 0.0
                    for index, name in enumerate(header) if name})
                rows.append(row)
        selected = []
        for endpoint in convergence['endpoints']:
            if endpoint['cycle'] <= 10:
                continue
            prior = [r for r in rows if r['t'] <= endpoint['t']]
            if not prior or endpoint['t']-prior[-1]['t'] > 2.5:
                failures.append(path.name+' cycle '+str(endpoint['cycle'])+' 끝 표본 시각 부족')
                continue
            row = prior[-1]
            selected.append(dict(cycle=endpoint['cycle'], resource_t=endpoint['t'], llm_t=row['t'],
                                 offset_seconds=row['t']-endpoint['t'], row_number=row['number'], values=row['values']))
        tail = selected[-30:]
        metrics = {}
        if len(tail) == 30:
            xs = [r['cycle'] for r in tail]
            xm = sum(xs)/len(xs)
            denominator = sum((x-xm)**2 for x in xs)
            for tag in header:
                if not tag:
                    continue
                ys = [r['values'][tag] for r in tail]
                ym = sum(ys)/len(ys)
                metrics[tag] = dict(first=ys[0], last=ys[-1], min=min(ys), max=max(ys), delta=ys[-1]-ys[0],
                                    slope_mib_per_cycle=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/denominator)
        else:
            failures.append(path.name+' 마지막30회 표본 부족')
        relevant = {k:v for k,v in metrics.items() if k in ('Graphics', 'Textures', 'RenderTargets', 'RHIUnused',
                    'RHITransientResources', 'RHIMisc', 'FD3D12RT', 'Total', 'Untracked', 'TrackedTotal', 'FMallocUnused') or k.startswith('RHIMisc/')}
        results.append(dict(file=path.name, rows=len(rows), selected=len(selected), tail=tail, tag_metrics=metrics,
                            graphics_and_rhi_tags=relevant,
                            top_positive_slopes=sorted([dict(tag=k, **v) for k,v in metrics.items() if v['slope_mib_per_cycle']>0],
                                                       key=lambda v:v['slope_mib_per_cycle'], reverse=True)[:15]))
    if not results:
        failures.append('LLM CSV 없음')
    return dict(status='FAIL' if failures else 'PASS', failures=failures, trackers=results,
                scope='LLM 관측·태그 비교 완료 상태이며 GPU 비증가 게이트를 대체하지 않음.',
                method='UE 로그의 Wrote LLM csv line 번호를 원본 CSV 행에 대응. 플랫폼/기본 두 기록의 늦은 시각으로 보수적 정렬. 자원 끝점보다 앞선2.5초 이내 표본을 선택.',
                known_limits=['UE LLM 원본은0.01MiB 단위의 반올림 값. WDDM 프로세스 GPU 계정값과 측정 범위가 다름.',
                              '부모와 자식 태그를 더하면 중복 집계하므로 단순 합산하지 않음.',
                              '드라이버 내부·LLM 비추적 할당의 소유 스택과 누수 유무는 태그만으로 확정하지 못함.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--game-log', type=Path, required=True)
    parser.add_argument('--convergence', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    value = analyze(args.folder, args.game_log, json.loads(args.convergence.read_text(encoding='utf-8-sig')))
    args.out.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(status=value['status'], failures=value['failures'],
                         trackers=[dict(file=v['file'], selected=v['selected'], graphics_and_rhi_tags=v['graphics_and_rhi_tags'])
                                   for v in value['trackers']]), ensure_ascii=False, indent=2))
