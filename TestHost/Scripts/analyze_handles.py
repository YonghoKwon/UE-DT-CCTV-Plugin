#!/usr/bin/env python3
"""재생 구간 끝과 핸들 종류별 개수·살아남은 객체를 대조한다. 할당 스택 귀속은 추정하지 않는다."""
import argparse
import collections
import datetime as dt
import json
import re
from pathlib import Path


def analyze(raw, log, convergence):
    stamp = re.search(r'\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start',
                      log.read_text(encoding='utf-8-sig', errors='replace')).group(1)
    origin = dt.datetime.strptime(stamp, '%Y.%m.%d-%H.%M.%S:%f').replace(tzinfo=dt.timezone.utc)
    points = convergence['endpoints']
    selected, previous, index, durations, snapshots = [], None, 0, [], 0
    with raw.open(encoding='utf-8-sig') as stream:
        for line in stream:
            sample = json.loads(line)
            sample['t'] = (dt.datetime.fromisoformat(sample['utc'])-origin).total_seconds()
            snapshots += 1
            durations.append(sample['collection_seconds'])
            while index < len(points) and points[index]['t'] < sample['t']:
                if previous is not None:
                    point = points[index]
                    selected.append(dict(cycle=point['cycle'], resource_t=point['t'], snapshot_t=previous['t'],
                                         offset_seconds=previous['t']-point['t'], resource_handles=point['handles'],
                                         total=previous['total'], counts=previous['counts'], handles=previous['handles']))
                index += 1
            previous = sample
    if index < len(points) and previous is not None and previous['t'] <= points[index]['t']:
        point = points[index]
        selected.append(dict(cycle=point['cycle'], resource_t=point['t'], snapshot_t=previous['t'],
                             offset_seconds=previous['t']-point['t'], resource_handles=point['handles'],
                             total=previous['total'], counts=previous['counts'], handles=previous['handles']))
    window = [r for r in selected if r['cycle'] > 10]
    names = sorted({m for r in window for m in r['counts']})
    ranges = {m: dict(first=window[0]['counts'].get(m, 0), last=window[-1]['counts'].get(m, 0),
                      min=min(r['counts'].get(m, 0) for r in window), max=max(r['counts'].get(m, 0) for r in window)) for m in names}
    first, last = window[0], window[-1]
    def identity(r):
        return r['handle'], r['object'], r['type']
    initial = {identity(r) for r in first['handles']}
    final = {identity(r) for r in last['handles']}
    unknown = sum(v for r in window for k, v in r['counts'].items() if k.startswith('미확인_'))
    quality = len(selected) == len(points) and not unknown and all(-2.5 <= r['offset_seconds'] <= 0 for r in selected)
    return dict(status='PASS' if quality else 'FAIL', scope='읽기 전용 핸들 원인 관측 완료 상태, 자원 합격 판정을 대체하지 않음.',
                snapshots=snapshots, selected=len(selected), unknown_type_queries=unknown,
                collection_seconds_max=max(durations), collection_seconds_mean=sum(durations)/len(durations),
                type_ranges=ranges,
                persistent_to_final_added_types=dict(collections.Counter(r[2] for r in final-initial)),
                initial_removed_types=dict(collections.Counter(r[2] for r in initial-final)),
                selected_endpoints=[{k:v for k,v in r.items() if k != 'handles'} for r in selected],
                note='핸들 값과 커널 객체 식별자·종류만 수집. 이름·파일경로·객체 내용은 수집하지 않음. 종류 증감만으로 플러그인/엔진/드라이버 할당 스택이나 누수 유무를 단정하지 않음.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('raw', type=Path)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--convergence', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.raw, args.log, json.loads(args.convergence.read_text(encoding='utf-8-sig')))
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k != 'selected_endpoints'}, ensure_ascii=False, indent=2))
