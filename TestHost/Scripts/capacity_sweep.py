#!/usr/bin/env python3
"""CCTV-01L 채널 수 시험을 직렬 실행하며 경계값의 두 번 통과를 확인한다."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
REPORT=ROOT/'Saved/Reports/CCTV-01L'
sys.path.insert(0,str(Path(__file__).parent))
from analyze_capacity import analyze

def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,j): p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def stamp(): return dt.datetime.now().astimezone().isoformat()

def save_case(tag):
    p=REPORT/'measurements'/tag
    run=read(p/'gate-result.json')
    count=run['count']
    hw=0 if run['codec_case']=='C8' else min(4,count)
    try:
        a=analyze(p,p/'game.log',p/'gpu.stdout.log',p/'cpu.csv',hw)
    except Exception as exc:
        a={'status':'FAIL','numeric_status':'FAIL','failures':['분석 준비 실패: '+str(exc)],'count':count,'codec_case':run['codec_case'],'mode':run['mode'],'tag':tag}
    if run['status']=='INVALID':a.update(status='INVALID',numeric_status='INVALID',failures=[run['note']])
    a['execution_status']=run['status']
    a['visual_status']='NOT_RUN' if run['mode']=='Windows' else 'ENV_BLOCKED'
    a['status']=a['numeric_status'] if run['mode']!='Windows' or a['numeric_status']!='PASS' else 'REVIEW_REQUIRED'
    write(p/'capacity-analysis.json',a)
    c=read(REPORT/'checkpoint.json')
    c['active_measurement']={};c['updated_at']=stamp();c['phase']='CAPACITY_MEASUREMENTS'
    c['gates'][tag]=a
    c['measurements']=[x for x in c['measurements'] if x['tag']!=tag]+[{'tag':tag,'codec':run['codec_case'],'mode':run['mode'],'count':count,'status':a['status']}]
    write(REPORT/'checkpoint.json',c)
    with (REPORT/'progress.md').open('a',encoding='utf-8') as f:
        f.write(stamp()+' | '+tag+' | '+a['status']+' | '+json.dumps({k:a.get(k) for k in ['seconds','decode_fps','display_fps','decoder_counts','failures']},ensure_ascii=False)+'\n')
    print(tag,a['numeric_status'],a.get('seconds'),a.get('decode_fps'),flush=True)
    return a

def run_case(codec,mode,count,attempt):
    c=read(REPORT/'checkpoint.json')
    if c.get('deadline_enabled',True) and dt.datetime.now(dt.timezone.utc)>=dt.datetime.fromisoformat(c['deadline_utc']):raise TimeoutError('4시간 실행 예산 종료')
    tag='{}-{}-n{}-a{}'.format(codec,mode,count,attempt)
    p=REPORT/'measurements'/tag
    if (p/'capacity-analysis.json').exists():return read(p/'capacity-analysis.json')
    if (p/'gate-result.json').exists():return save_case(tag)
    command=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'TestHost/Scripts/capacity_case.ps1'),'-Codec',codec,'-Mode',mode,'-Count',str(count),'-Tag',tag]
    with (REPORT/(tag+'.wrapper.log')).open('w',encoding='utf-8') as log:
        result=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode==3:raise RuntimeError('BLOCKED_BY_OTHER_WORK')
    if not (p/'gate-result.json').exists():raise RuntimeError('측정 실행 결과 없음: '+tag)
    a=save_case(tag)
    if a['numeric_status']=='INVALID':
        # 오염된 시도는 한 조합 한 번에 포함하지 않고 같은 단계에서 다시 시도한다.
        return run_case(codec,mode,count,attempt+1)
    return a

def sweep(codec,mode):
    p=REPORT/'boundaries';p.mkdir(exist_ok=True)
    target=p/(codec+'-'+mode+'.json')
    if target.exists() and read(target).get('complete'):return
    known={};low=0;high=None
    for n in [4,8,12,16,20,24,32]:
        a=run_case(codec,mode,n,1);known[n]=a
        if a['numeric_status']=='PASS':low=n
        else:high=n;break
    if high is not None:
        for n in range(low+2,high,2):
            a=run_case(codec,mode,n,1);known[n]=a
            if a['numeric_status']=='PASS':low=n
            else:high=n;break
    confirmed=0
    while low>=2:
        first=known.get(low) or run_case(codec,mode,low,1)
        second=run_case(codec,mode,low,2) if first['numeric_status']=='PASS' else first
        if first['numeric_status']==second['numeric_status']=='PASS':confirmed=low;break
        high=low
        low-=2
    v={'codec':codec,'mode':mode,'complete':True,'confirmed_max_channels':confirmed,'at_least':confirmed==32,'first_failure_channels':high,'status':'REVIEW_REQUIRED' if mode=='Windows' and confirmed else 'PASS' if confirmed else 'FAIL','completed_at':stamp(),'note':'경계값은 두 번 수치 통과. Windows는 별도 PNG 직접 검수 후 최종 인정. Linux는 nullrhi 디코딩만 인정.'}
    write(target,v)
    with (REPORT/'progress.md').open('a',encoding='utf-8') as f:f.write(stamp()+' | 경계 '+codec+' '+mode+' | '+v['status']+' | '+str(confirmed)+'채널, 상위실패='+str(high)+'\n')
    print('BOUNDARY',codec,mode,confirmed,flush=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--codec');args=parser.parse_args()
    c=read(REPORT/'checkpoint.json')
    try:
        for codec in ([args.codec] if args.codec else c['priority']):
            for mode in ['Windows','LinuxSW','LinuxCUDA']:sweep(codec,mode)
    except (TimeoutError,RuntimeError) as exc:
        c=read(REPORT/'checkpoint.json');c.update(status='BLOCKED_BY_OTHER_WORK' if 'BLOCKED_BY_OTHER_WORK' in str(exc) else 'INCOMPLETE',phase='STOPPED',stop_reason=str(exc),updated_at=stamp());write(REPORT/'checkpoint.json',c)
        print(str(exc),flush=True);return 3
    return 0

if __name__=='__main__':raise SystemExit(main())
