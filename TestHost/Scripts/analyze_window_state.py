from pathlib import Path
import argparse
import collections
import csv
import datetime as dt
import importlib.util
import json
import statistics

parser=argparse.ArgumentParser()
parser.add_argument('tag')
parser.add_argument('--report-root',default='Saved/Reports/CCTV-01W')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
report=(root/args.report_root).resolve()
folder=report/'measurements'/args.tag
def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def csv_rows(p):
    with p.open(encoding='utf-8-sig',newline='') as f: return list(csv.DictReader(f))
execution=read(folder/'execution.json')
monitor=read(folder/'window-monitor-result.json')
rows=csv_rows(folder/'samples.csv')
first=next(r for r in rows if float(r['t'])>=10)
last=next(r for r in rows if float(r['t'])>=float(first['t'])+90)
start,end=float(first['t']),float(last['t'])
seconds=end-start
steady=[r for r in rows if start<=float(r['t'])<=end]
window=[json.loads(s) for s in (folder/'window-state.jsonl').read_text(encoding='utf-8-sig').splitlines() if s.strip()]
window=[r for r in window if start<=r['game_t']<=end]
condition=execution['condition']
gate_key=condition
timer_option=execution.get('ignore_timer_throttling',-1)
if timer_option>=0:
    gate_key=condition+'-timer-'+str(timer_option)
if condition=='W5' and execution['power_bits']!=5:
    gate_key={1:'W5-speed-only',4:'W5-timer-only'}[execution['power_bits']]
failures=[]
if execution['status']!='MEASURED' or execution.get('exit_code')!=0: failures.append('패키지 실행 미완료/비정상 종료')
if monitor['status']!='PASS': failures.append('창 상태 관측기 실패: '+monitor['note'])
if not window: failures.append('관측 구간의 창 상태 표본 없음')
if condition=='W1' and not all(r['foreground_pid']==r['pid'] and not r['minimized'] for r in window): failures.append('포그라운드 유지 조건 이탈')
if condition in ('W2','W5'):
    if not all(r['foreground_pid']==r['cover_pid'] and not r['minimized'] and 0<r['cover_intersection_pixels']<(r['window_rect']['Right']-r['window_rect']['Left'])*(r['window_rect']['Bottom']-r['window_rect']['Top']) for r in window): failures.append('중립 창 포그라운드/일부 가림 조건 이탈')
if condition=='W3' and not all(r['minimized'] for r in window): failures.append('최소화 유지 조건 이탈')
if condition in ('W1','W2','W5') and not all(r['client_w']==1920 and r['client_h']==1080 for r in window): failures.append('1920x1080 클라이언트 크기 이탈')
if condition=='W5':
    bits=execution['power_bits']
    if not all(r['power']['Ok'] and r['power']['ControlMask']&bits==bits and r['power']['StateMask']&bits==0 for r in window): failures.append('전원 제한 명시 비활성 상태 이탈')
    if monitor['power_restore']!='PASS': failures.append('원래 전원 상태 명시 복원 미확인')
if timer_option>=0:
    log=(folder/'game.log').read_text(encoding='utf-8-sig',errors='replace')
    expected='WindowsTimerThrottling enabled='+str(timer_option)+' source=command_line command_line=valid result='+('applied' if timer_option else 'disabled')
    if log.count(expected)!=1: failures.append('플러그인 옵션 결과 한 줄 확인 실패')
    if condition!='W2': failures.append('옵션 비교의 W2 조건 아님')
    for r in window:
        power=r['power']
        if not power['Ok'] or power['ControlMask']&4!=(4 if timer_option else 0) or power['StateMask']&4!=0:
            failures.append('플러그인 타이머 정책 관측값 불일치');break
        if power['ControlMask']&1!=monitor['power_original']['ControlMask']&1 or power['StateMask']&1!=monitor['power_original']['StateMask']&1:
            failures.append('실행 속도 정책 변경 감지');break
spec=importlib.util.spec_from_file_location('dtcctv_latency',root/'TestHost/Scripts/analyze_latency.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
channel_rows=csv_rows(folder/'samples.channels.csv')
channel_t=max(float(r['t']) for r in channel_rows if float(r['t'])<=end)
channels=[dict(channel=int(r['channel']),**module.age_summary(r)) for r in channel_rows if float(r['t'])==channel_t]
ages=[r['p95_ms'] for r in channels if r['samples']>0]
decoded=int(last['decoded'])-int(first['decoded'])
displayed=int(last['displayed'])-int(first['displayed'])
replaced=int(last['superseded'])-int(first['superseded'])
decode_fps=decoded/(16*seconds);display_fps=displayed/(16*seconds)
replacement=replaced/decoded if decoded else None
frame_fps=[float(r['frame_fps']) for r in steady]
fps_fail=[]
if decode_fps<58 or display_fps<58: fps_fail.append('디코딩/표시58fps 기준 미달')
if max(int(r['dropped']) for r in rows) or max(int(r['reconnects']) for r in rows) or max(int(r['errors']) for r in rows): fps_fail.append('손실/재연결/오류0 미달')
if not all(int(r['playing'])==16 for r in steady): fps_fail.append('전16채널 Playing 미달')
if seconds<60: fps_fail.append('유효60초 미달')
if not ages or max(ages)>50: fps_fail.append('채널별p95 50ms 미달/표본 없음')
if replacement is None or replacement>.01: fps_fail.append('프레임 교체1% 미달')
gpu=csv_rows(folder/'gpu.csv')
window_start=dt.datetime.fromisoformat(window[0]['utc'].replace('Z','+00:00')) if window else None
window_end=dt.datetime.fromisoformat(window[-1]['utc'].replace('Z','+00:00')) if window else None
gpu=[r for r in gpu if r.get('utc') and window_start and window_start<=dt.datetime.fromisoformat(r['utc'].replace('Z','+00:00'))<=window_end]
gpu_summary={}
for key in ['utilization_gpu','utilization_memory','memory_mib','sm_mhz','temperature_c','power_w']:
    values=[]
    for row in gpu:
        try: values.append(float(row[key].strip()))
        except (ValueError,TypeError,KeyError): pass
    if values: gpu_summary[key]=dict(mean=statistics.mean(values),min=min(values),max=max(values))
value=dict(status='INVALID' if failures else 'MEASURED',condition=gate_key,base_condition=condition,power_bits=execution['power_bits'],tag=args.tag,
    timer_option=timer_option,
    measurement_valid=not failures,condition_failures=failures,fps_reference_status='FAIL' if fps_fail else 'PASS',fps_failures=fps_fail,
    first_t=start,last_t=end,seconds=seconds,game_fps_limit=60,video_fps=60,decode_fps=decode_fps,display_fps=display_fps,
    actual_game_fps_mean=statistics.mean(frame_fps),game_frame_ms_mean_from_1s_fps=statistics.mean(1000/v for v in frame_fps if v>0),
    worst_1s_max_frame_ms=max(float(r['max_frame_ms']) for r in steady),
    superseded=replaced,replacement_rate=replacement,worst_channel_p95_ms=max(ages) if ages else None,
    dropped=max(int(r['dropped']) for r in rows),reconnects=max(int(r['reconnects']) for r in rows),errors=max(int(r['errors']) for r in rows),
    all_playing=all(int(r['playing'])==16 for r in steady),output_w_positive=all(int(r['output_w'])>0 for r in steady),
    channel_ages=channels,window_samples=len(window),foreground_sample_fraction=sum(r['foreground_pid']==r['pid'] for r in window)/len(window) if window else None,
    minimized_sample_fraction=sum(r['minimized'] for r in window)/len(window) if window else None,
    cpu_percent_mean=statistics.mean(r['cpu_percent'] for r in window) if window else None,
    cpu_percent_max=max(r['cpu_percent'] for r in window) if window else None,gpu_global=gpu_summary,
    power_states=[dict(control=c,state=s,count=n) for (c,s),n in collections.Counter((r['power']['ControlMask'],r['power']['StateMask']) for r in window if r['power']['Ok']).items()],
    power_original=monitor.get('power_original'),power_applied=monitor.get('power_applied'),power_restore=monitor['power_restore'],
    method='초기10초 제외 뒤 연속90초 이상의 최초CSV쌍. FPS는 누적 프레임 차이/16/실제 시간. 지연은 같은 종료 시각의 채널 누적히스토그램. CPU는 전체 논리CPU 수로 정규화. GPU는 송출 NVENC도 포함한 전체 장치5초 표본. 창/전원은 약1초 표본이며 CUA 호출0.')
(folder/'analysis.json').write_bytes((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
checkpoint=read(report/'checkpoint.json')
target=checkpoint.setdefault('followup',{}).setdefault('gates',{}) if timer_option>=0 else checkpoint['gates']
target[gate_key]=value
checkpoint['updated_at']=dt.datetime.now().astimezone().isoformat();checkpoint['active_measurement']={}
checkpoint['measurements'].append(dict(tag=args.tag,condition=gate_key,path=str(folder),status=value['status']))
(report/'checkpoint.json').write_bytes((json.dumps(checkpoint,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
with (report/'progress.md').open('a',encoding='utf-8') as f:
    f.write(checkpoint['updated_at']+' | '+gate_key+' | '+value['status']+' | decode={:.4f}, display={:.4f}, p95={}, 교체={:.4%}, game={:.3f}fps, 조건유효={}, 기존성능기준={}\n'.format(decode_fps,display_fps,value['worst_channel_p95_ms'],replacement or 0,value['actual_game_fps_mean'],value['measurement_valid'],value['fps_reference_status']))
print(json.dumps({k:value[k] for k in ['condition','status','condition_failures','seconds','decode_fps','display_fps','worst_channel_p95_ms','replacement_rate','actual_game_fps_mean','foreground_sample_fraction','cpu_percent_mean','power_states','power_restore','fps_reference_status']},ensure_ascii=False))
