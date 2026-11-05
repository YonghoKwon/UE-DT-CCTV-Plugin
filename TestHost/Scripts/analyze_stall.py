#!/usr/bin/env python3
"""F1 정지의 패킷 보존·새 제출 지연 회복·키프레임 대기 관측을 판정한다."""
import argparse
import csv
import json
from pathlib import Path


def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as stream:
        return list(csv.DictReader(stream))


def analyze(folder, kind, limit_ms):
    events=rows(folder/'samples.events.csv')
    prefix='render-stall' if kind=='render' else 'stall'
    begin=next(float(r['t']) for r in events if r['event']==prefix+'-begin')
    end=next(float(r['t']) for r in events if r['event']==prefix+'-end')
    samples=rows(folder/'samples.csv')
    channel_rows=rows(folder/'samples.channels.csv')
    groups={}
    for r in channel_rows:
        if float(r['t'])>=end:groups.setdefault(float(r['t']),[]).append(r)
    ordered=[(t,g) for t,g in sorted(groups.items()) if len(g)==16]
    baseline={int(r['channel']):int(r['frame_age_samples']) for r in ordered[0][1]}
    recovery=None
    for t,group in ordered[1:]:
        if all(int(r['frame_age_samples'])>baseline[int(r['channel'])] and float(r['frame_age_latest_ms'])<=limit_ms and int(r['state'])==2 for r in group):
            recovery=t-end
            break
    before=[r for r in samples if float(r['t'])<begin][-1]
    after=next(r for r in samples if float(r['t'])>=end)
    stalled_rate=(int(after['decoded'])-int(before['decoded']))/(16*(float(after['t'])-float(before['t'])))
    result=dict(status='FAIL',kind=kind,begin_t=begin,end_t=end,actual_stall_seconds=end-begin,
                recovery_upper_seconds=recovery,frame_age_limit_ms=limit_ms,
                dropped=max(int(r['dropped']) for r in samples),reconnects=max(int(r['reconnects']) for r in samples),
                decode_errors=max(int(r['errors']) for r in samples),
                superseded=max(int(r['superseded']) for r in samples),
                peak_queued_packets=max(int(r['peak_queued_packets']) for r in samples),
                packet_key_waits=max(int(r['packet_key_waits']) for r in samples),
                packet_key_wait_seconds=max(float(r['packet_key_wait_seconds']) for r in samples),
                observed_decode_fps_across_stall=stalled_rate,
                screenshots=len(list(folder.glob('*.png'))),visual_status='NOT_RUN',failures=[],
                method='정지종료이후첫CSV를기준으로모든채널제출수가증가하고최근age가기준이하인첫후속표본의관측상한. GT/RHI정지중의전구간FPS는합격조건으로대체하지않음. 키대기는패킷큐누적전수계측.')
    if result['dropped']:result['failures'].append('PacketsDropped 0 기준 미달')
    if result['reconnects']:result['failures'].append('재연결0 기준 미달')
    if result['decode_errors']:result['failures'].append('디코딩오류 발생')
    if recovery is None or recovery>2:result['failures'].append('정지후2초 frame age 회복 기준 미달')
    if not result['screenshots']:result['failures'].append('정지후 PNG 미수집')
    result['status']='FAIL' if result['failures'] else 'PASS'
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('folder',type=Path)
    parser.add_argument('--kind',choices=['gt','render'],default='gt')
    parser.add_argument('--limit-ms',type=float,default=34)
    args=parser.parse_args()
    value=analyze(args.folder,args.kind,args.limit_ms)
    (args.folder/'stall-analysis.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(value,ensure_ascii=False,indent=2))
