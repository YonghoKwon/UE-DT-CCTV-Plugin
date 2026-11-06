#!/usr/bin/env python3
"""TCP 대조 실험의 원시 표본을 요약한다. 미달값과 실행 오류를 구분한다."""
from pathlib import Path
import argparse,csv,json,re,statistics

def textfile(path):
    data=path.read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig',errors='replace')

def analyze(case):
    execution=json.loads((case/'execution.json').read_text(encoding='utf-8-sig'))
    value=dict(execution);fail=[];channels=[]
    resultfile=case/'runtime-result.json'
    runtime=json.loads(resultfile.read_text()) if resultfile.exists() else {}
    recovered=execution['mode']=='plugin' and runtime.get('exit_codes')==[2] and '점검 결과 WARN' in (case/'game.log').read_text(encoding='utf-8-sig',errors='replace')
    if execution['status']!='MEASURED' and not recovered:
        value.update(status='NOT_MEASURED',failures=[execution['note']]);return value
    files=sorted(case.rglob('channel-*.csv')) if execution['mode']=='plugin' else sorted(case.glob('receiver-*.jsonl'))
    for path in files:
        if path.suffix=='.csv':
            with path.open(encoding='utf-8-sig',newline='') as stream:rows=list(csv.DictReader(stream))
        else:rows=[json.loads(x) for x in path.read_text(encoding='utf-8').splitlines() if x.strip()]
        eligible=[r for r in rows if float(r['t'])>=10]
        if len(eligible)<2:fail.append(str(path.name)+' 표본 부족');continue
        a,b=eligible[0],eligible[-1];seconds=float(b['t'])-float(a['t'])
        key='decoded' if execution['mode']=='plugin' else 'frames' if execution['mode']=='decode' else 'packets'
        fps=(int(b[key])-int(a[key]))/seconds if seconds>0 else 0
        item=dict(file=str(path.relative_to(case)),seconds=seconds,fps=fps,errors=int(b['errors']))
        if execution['mode']=='plugin':
            item.update(reconnects=int(b['reconnects']),dropped=int(b['dropped']),playing=all(int(r['state'])==2 for r in eligible),
                peak_queued_packets=int(b['peak_queued_packets']),queued_packets_max=max(int(r['queued_packets']) for r in eligible),
                read_wait_fraction=(float(b['read_wait_seconds'])-float(a['read_wait_seconds']))/seconds,
                read_wait_max_seconds=float(b['read_wait_max_seconds']),read_gap_max_seconds=float(b['read_gap_max_seconds']),
                packet_push_seconds=float(b['packet_push_seconds']),packet_push_max_seconds=float(b['packet_push_max_seconds']))
            if item['reconnects'] or item['dropped'] or not item['playing']:fail.append(path.name+' 재연결/손실/Playing 기준 미달')
        if seconds<120:fail.append(path.name+' 유효 구간120초 미달')
        if fps<29.1:fail.append(path.name+' 원본97% 미달')
        if item['errors']:fail.append(path.name+' 디코딩/읽기 오류')
        channels.append(item)
    if len(channels)!=execution['channels']:fail.append('채널 수 불일치')
    socket_samples=[]
    for path in sorted(case.glob('ss-*.log')):
        text=textfile(path)
        socket_samples+=re.findall(r'ESTAB\s+(\d+)\s+\d+[^\n]*:8554\s*\n\s*skmem:\(r(\d+),rb(\d+),[^\n]*',text)
    rawresources=[json.loads(x) for x in (case/'resources.jsonl').read_text(encoding='utf-8').splitlines() if x.strip()]
    cpu=[];rss=[];threadmax=0;waits={}
    for a,b in zip(rawresources,rawresources[1:]):
        ta={p['pid']:p['cpu_ticks'] for p in a['processes']};elapsed=b['t']-a['t']
        if elapsed<=0:continue
        cpu.append(sum(p['cpu_ticks']-ta.get(p['pid'],p['cpu_ticks']) for p in b['processes'])/100/elapsed/16*100)
    for r in rawresources:
        rss.append(sum(p['rss_pages'] for p in r['processes'])*4096/1048576)
        threadmax=max(threadmax,sum(p['threads'] for p in r['processes']))
        for p in r['processes']:
            for v in p['wchan'].values():
                name=v['name']+':'+v['wchan'];waits[name]=waits.get(name,0)+1
    slow=[]
    for path in case.glob('server.*.log'):
        slow+=re.findall(r'[^\n]*reader is too slow[^\n]*',path.read_text(encoding='utf-8-sig',errors='replace'))
    value.update(status='FAIL' if fail else 'PASS',measurement_valid=True,execution_reassessment='점검 종료2의 WARN을 실행 실패로 오해한 원시 상태 보존,120초 이상 CSV로 디코딩 범위 재판정' if recovered else None,failures=fail,channel_metrics=channels,decode_or_copy_fps_mean=statistics.mean(x['fps'] for x in channels) if channels else None,
        decode_or_copy_fps_min=min((x['fps'] for x in channels),default=None),cpu_mean_percent_of_16=statistics.mean(cpu) if cpu else None,
        rss_peak_mib=max(rss,default=None),threads_peak=threadmax,thread_wait_samples=waits,reader_too_slow_events=len(slow),
        socket=dict(samples=len(socket_samples),recv_q_max_bytes=max((int(x[0]) for x in socket_samples),default=None),
            rmem_alloc_max_bytes=max((int(x[1]) for x in socket_samples),default=None),receive_buffer_min_bytes=min((int(x[2]) for x in socket_samples),default=None),receive_buffer_max_bytes=max((int(x[2]) for x in socket_samples),default=None)),
        limits=['Linux nullrhi 디코딩 범위. 표시·렌더링 미검증.','FFmpeg 복사는 비디오 패킷 수신 후 해제, 디코딩은 SW2스레드이며 색 변환·출력 없음.','CPU는16논리 프로세서 정규화. 소켓 rb는 커널 회계값이며 Linux SO_RCVBUF 배수에 주의.'])
    return value

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('case',type=Path);a=p.parse_args();v=analyze(a.case)
    (a.case/'analysis.json').write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v.get(k) for k in ('tag','status','decode_or_copy_fps_mean','decode_or_copy_fps_min','reader_too_slow_events')},ensure_ascii=False))
