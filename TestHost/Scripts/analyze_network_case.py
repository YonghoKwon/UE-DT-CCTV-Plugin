"""장애 적용 구간과 원시 채널 CSV를 대조한다. 미측정 항목은 수치로 만들지 않는다."""
from pathlib import Path
import argparse,csv,datetime as dt,json,re,statistics

def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def lines(path):return [json.loads(x.lstrip('\ufeff')) for x in path.read_text(encoding='utf-8-sig').splitlines() if x.strip()]
def stamp(value):return dt.datetime.fromisoformat(value.replace('Z','+00:00')).timestamp()
def analyze(case):
    ex=read(case/'execution.json');out=dict(status='FAIL',execution=ex,metrics=None,failures=[],unverified=[])
    if ex['status']!='MEASURED':
        out['status']='ENV_BLOCKED' if ex['status']=='ENV_BLOCKED' else 'FAIL'
        out['failures']=[ex['note']];return out
    events=lines(case/'network-events.jsonl');event={x['event']:x for x in events}
    start=event['impairment-start']['t'];clear=event['impairment-clear-before']['t']
    channels={}
    if ex['platform']=='linux':
        for p in sorted((case/'saved').rglob('channel-*.csv')):channels[int(p.stem.split('-')[-1])]=rows(p)
    else:
        for x in rows(case/'samples.channels.csv'):channels.setdefault(int(x['channel'])+1,[]).append(x)
    if len(channels)!=16:out['failures'].append('16채널 CSV 부족');return out
    common=set.intersection(*[set(float(x['t']) for x in v) for v in channels.values()])
    t0=next(t for t in sorted(common) if t>=start+10)
    t1=next(t for t in sorted(common) if t>=t0+120 and t<=clear)
    per=[]
    for n,seq in channels.items():
        indexed={float(x['t']):x for x in seq};a=indexed[t0];b=indexed[t1]
        interval=[x for x in seq if t0<=float(x['t'])<=t1]
        per.append(dict(channel=n,decode_fps=(int(b['decoded'])-int(a['decoded']))/(t1-t0),display_fps=(int(b['displayed'])-int(a['displayed']))/(t1-t0),
            reconnects=int(b['reconnects'])-int(a['reconnects']),dropped=int(b['dropped'])-int(a['dropped']),decode_errors=int(b['errors'])-int(a['errors']),
            playing_throughout=all(int(x['state'])==2 for x in interval),last_decoded=int(seq[-1]['decoded']),last_reconnects=int(seq[-1]['reconnects'])))
    metrics=dict(seconds=t1-t0,first_t=t0,last_t=t1,decode_fps=statistics.mean(x['decode_fps'] for x in per),min_channel_decode_fps=min(x['decode_fps'] for x in per),
                 reconnects=sum(x['reconnects'] for x in per),dropped=sum(x['dropped'] for x in per),decode_errors=sum(x['decode_errors'] for x in per),
                 all_playing=all(x['playing_throughout'] for x in per),channels=per,aggregate_baseline_bps=ex.get('aggregate_baseline_bps'))
    out['metrics']=metrics
    # Linux 점검 도구는 WARN도 정상 종료0을 사용하므로 프로세스 생존과 CSV로 별도 판정한다.
    log=(case/'game.log').read_text(encoding='utf-8-sig',errors='replace')
    native=ex.get('native_exit_code')
    intended_exit=False
    if ex['platform']=='linux':
        reports=list((case/'saved').rglob('report.json'))
        if len(reports)==1:
            report=read(reports[0]);intended_exit=native in (0,1,2) and report.get('tool')=='DTCctvCheck'
            metrics['native_check_verdict']=report.get('verdict')
    crash=bool(re.search(r'Fatal error|Unhandled Exception|SIGSEGV|Assertion failed:',log)) or (native!=0 and not intended_exit)
    metrics['crash_observed']=crash
    if crash:out['failures'].append('크래시/비정상 종료')
    if ex['condition'] in ('N1','N2','N7'):
        if metrics['min_channel_decode_fps']<29.1:out['failures'].append('채널별 디코딩 원본97% 미달')
        if metrics['reconnects']:out['failures'].append('재연결0 기준 미달')
        if not metrics['all_playing']:out['failures'].append('전 채널 재생 유지 미달')
        out['unverified'].append('네트워크 포함 지연을 측정하지 못함. 디코드 반환~제출 frame age에서 추가 지연을 빼지 않음.')
    if ex['condition']=='N3':
        if metrics['reconnects'] or not metrics['all_playing']:out['failures'].append('손실0.5% 재생 유지/재연결0 미달')
        if ex['transport']=='udp':
            metrics['udp_corrupt_frames']=None
            out['unverified'].append('DecodeErrors는 모든 손상 프레임 수가 아님. 정확한 손상 프레임 계수는 미측정.')
    post=[]
    for n,seq in channels.items():
        tail=[x for x in seq if float(x['t'])>=clear+5]
        progressing=bool(len(tail)>2 and int(tail[-1]['decoded'])>int(tail[-2]['decoded']) and int(tail[-1]['state'])==2)
        stable=bool(tail and int(tail[-1]['reconnects'])==int(tail[0]['reconnects']))
        post.append(dict(channel=n,playing_and_progressing=progressing,no_repeated_reconnect_after_clear=stable))
    metrics['post_clear_channels']=post
    if ex['condition'] in ('N4','N5') and not all(x['playing_and_progressing'] and x['no_repeated_reconnect_after_clear'] for x in post):out['failures'].append('장애 해제 후 재생/재연결 안정화 미달')
    if ex['condition']=='N6':
        earliest=event['outage-before']['t']+10;observed=event['outage-restored-observed']['t'];recovery=[]
        for n,seq in channels.items():
            baseline=max((x for x in seq if float(x['t'])<=earliest),key=lambda x:float(x['t']))
            recovered=next((float(x['t']) for x in seq if float(x['t'])>=observed and int(x['state'])==2 and int(x['decoded'])>int(baseline['decoded'])),None)
            recovery.append(None if recovered is None else recovered-earliest)
        metrics['recovery_upper_seconds']=max(recovery) if all(x is not None for x in recovery) else None
        if metrics['recovery_upper_seconds'] is None or metrics['recovery_upper_seconds']>5:out['failures'].append('단절 복구 후5초 전채널 재생 기준 미달')
        out['unverified'].append('단절 중 마지막 화면 유지의 직접 검수 필요')
    if ex['platform']=='linux':out['unverified'].append('NullRHI: 화면/렌더 정상 여부는 ENV_BLOCKED. 디코딩 실측과 구분.')
    elif not (case/'visual-review.json').exists():out['unverified'].append('Windows PNG 직접 검수 대기')
    else:
        if read(case/'visual-review.json')['status']!='PASS':out['failures'].append('PNG 직접 검수 실패')
    metrics['adjusted_network_inclusive_latency_ms']=None
    clocks=lines(case/'game-clock.jsonl');anchor=next(x for x in clocks if x['t']>=10);origin=stamp(anchor['file_utc'])-anchor['t']
    resources=lines(case/'resources.jsonl') if ex['platform']=='linux' else rows(case/'resources.csv')
    normalized=[]
    for x in resources:
        private=x.get('private_kb') if ex['platform']=='linux' else float(x['private_bytes'])/1024
        if private is not None:normalized.append(dict(t=stamp(x['utc'])-origin,private_kb=float(private),threads=int(x['threads'])))
    before=[x for x in normalized if 10<=x['t']<start];after=[x for x in normalized if x['t']>=clear+20]
    if before and after:
        memory=dict(baseline_private_peak_kb=max(x['private_kb'] for x in before),post_private_peak_kb=max(x['private_kb'] for x in after),baseline_threads_peak=max(x['threads'] for x in before),post_threads_peak=max(x['threads'] for x in after))
        memory['private_delta_kb']=memory['post_private_peak_kb']-memory['baseline_private_peak_kb'];memory['threads_delta']=memory['post_threads_peak']-memory['baseline_threads_peak'];metrics['resources']=memory
        if memory['private_delta_kb']>0 or memory['threads_delta']>0:out['failures'].append('조건 해제 뒤 자원 원래 수준 복귀 미달(원시 기준)')
    else:out['unverified'].append('전후 자원 표본 부족')
    nstat={}
    for kind in ['before','after']:
        text=(case/f'nstat-{kind}.log').read_text(encoding='utf-16' if (case/f'nstat-{kind}.log').read_bytes().startswith(b'\xff\xfe') else 'utf-8-sig',errors='replace')
        nstat[kind]={m[0]:int(m[1]) for m in re.findall(r'^(Tcp\S+)\s+(\d+)\s',text,re.M)}
    metrics['tcp_retransmission_counters']={k:nstat['after'].get(k,0)-v for k,v in nstat['before'].items() if 'Retrans' in k}
    out['status']='FAIL' if out['failures'] else 'ENV_BLOCKED' if out['unverified'] else 'PASS'
    return out
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('case',type=Path);args=parser.parse_args()
    try:value=analyze(args.case)
    except Exception as e:value=dict(status='FAIL',metrics=None,failures=['분석 실행 오류: '+str(e)],unverified=[])
    (args.case/'analysis.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    root=args.case.parents[1];checkpoint=root.parent/'checkpoint.json';state=read(checkpoint)
    state['retest']['gates'][args.case.name]=value;state['active_measurement']={};state['updated_at']=dt.datetime.now().astimezone().isoformat()
    checkpoint.write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    summary={k:value.get(k) for k in ['status','failures','unverified']};m=value.get('metrics')
    if m:summary.update({k:m.get(k) for k in ['seconds','decode_fps','min_channel_decode_fps','reconnects','dropped','decode_errors','resources','recovery_upper_seconds']})
    with (root.parent/'progress.md').open('a',encoding='utf-8') as f:f.write(f"{state['updated_at']} | {args.case.name} | {value['status']} | {json.dumps(summary,ensure_ascii=False)}\n")
    print(json.dumps(summary,ensure_ascii=False))
