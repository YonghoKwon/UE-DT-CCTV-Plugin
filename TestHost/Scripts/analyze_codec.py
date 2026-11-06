#!/usr/bin/env python3
"""코덱별 원본97%·지연·대체·채널별 디코더를 실제 CSV로 판정한다."""
import argparse
import collections
import csv
import datetime as dt
import json
import re
from pathlib import Path
from analyze_latency import age_summary


def read_json(path):
    data = path.read_bytes()
    return json.loads(data.decode('utf-16') if data.startswith((b'\xff\xfe', b'\xfe\xff')) else data.decode('utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def analyze(folder, log, gpu_log, cpu_log, expected_hw=4):
    execution = read_json(folder/'gate-result.json')
    mode, fps = execution['mode'], execution['source_fps']
    windows = mode == 'Windows'
    failures = []
    aggregate = rows(folder/'samples.csv')
    steady = [r for r in aggregate if float(r['t']) >= 10]
    first, last = steady[0], steady[-1]
    duration = float(last['t'])-float(first['t'])
    channel_rows = rows(folder/'samples.channels.csv')
    latest_t = max(float(r['t']) for r in channel_rows)
    latest = [r for r in channel_rows if float(r['t']) == latest_t]
    channels = []
    for channel in range(16):
        group = [r for r in channel_rows if int(r['channel']) == channel]
        warm = [r for r in group if float(r['t']) >= 10]
        a, b = warm[0], warm[-1]
        seconds = float(b['t'])-float(a['t'])
        playing = [float(r['t']) for r in group if int(r['state']) == 2 and int(r['decoded']) > 0]
        item = dict(channel=channel, decode_fps=(int(b['decoded'])-int(a['decoded']))/seconds,
                    display_fps=(int(b['displayed'])-int(a['displayed']))/seconds,
                    first_playing_upper_seconds=min(playing) if playing else None,
                    decoder=b['decoder'], codec=b['codec'], source_width=int(b['src_w']))
        if windows:
            item['age'] = age_summary(b)
        channels.append(item)
    decoded = int(last['decoded'])-int(first['decoded'])
    displayed = int(last['displayed'])-int(first['displayed'])
    superseded = int(last['superseded'])-int(first['superseded'])
    counts = dict(collections.Counter(r['decoder'] for r in latest))
    if execution.get('exit_code') != 0 or duration < 80 or len(latest) != 16:
        failures.append('정상 종료·워밍업 제외80초·16채널 표본 기준 미달')
    if any(int(r['playing']) != 16 for r in steady):
        failures.append('10초 이후 전16채널 Playing 기준 미달')
    for key in ('dropped','errors','reconnects'):
        if max(int(r[key]) for r in aggregate):
            failures.append(key+' 0 기준 미달(시작 구간도 보존)')
    limit = fps*.97
    if decoded/(16*duration) < limit or any(r['decode_fps'] < limit for r in channels):
        failures.append('전체·채널별 디코딩 원본97% 기준 미달')
    age_limit = 70 if fps == 30 else 50
    if windows:
        if displayed/(16*duration) < limit or any(r['display_fps'] < limit for r in channels):
            failures.append('전체·채널별 표시 원본97% 기준 미달')
        if max(r['age']['p95_ms'] for r in channels) > age_limit:
            failures.append('채널별 frame age p95 기준 미달')
        if superseded/max(1,decoded) > .01:
            failures.append('대체율1% 기준 미달')
        if not (folder/'final.png').exists():
            failures.append('PNG 없음')
    hardware = 'd3d11va' if execution.get('force_d3d') else 'cuda'
    if mode == 'LinuxSW':
        if counts != {'software':16}:
            failures.append('소프트웨어16 경로 기준 미달')
    elif expected_hw >= 0 and (counts.get(hardware,0) != expected_hw or counts.get('software',0) != 16-expected_hw):
        failures.append('요청한 HW/SW 채널 분담 기준 미달')
    stamp = re.search(r'\[([^\]]+)\].*LogDTCctvHarness: 시나리오: start', log.read_text(encoding='utf-8-sig', errors='replace')).group(1)
    origin = dt.datetime.strptime(stamp,'%Y.%m.%d-%H.%M.%S:%f').replace(tzinfo=dt.timezone.utc)
    cpu = {}
    if cpu_log.exists():
        data = rows(cpu_log)
        for row in data:
            row['t'] = (dt.datetime.fromisoformat(row['utc'])-origin).total_seconds()
        for role in ('game','publisher'):
            groups = collections.defaultdict(list)
            for row in data:
                if row['role'] == role and 10 <= row['t'] <= float(last['t']):
                    groups[row['pid']].append(row)
            cores = 0.0
            for group in groups.values():
                if len(group)>1:
                    cores += (float(group[-1]['cpu_seconds'])-float(group[0]['cpu_seconds']))/(group[-1]['t']-group[0]['t'])
            if groups:
                cpu[role] = dict(cpu_core_equivalents=cores,host_cpu_percent=cores/16*100,logical_processors=16)
    linux_cpu = folder/'linux-cpu.csv'
    if not windows and linux_cpu.exists():
        data = rows(linux_cpu)
        for row in data:
            row['t'] = (dt.datetime.fromisoformat(row['utc'])-origin).total_seconds()
        data = [r for r in data if 10 <= r['t'] <= float(last['t'])]
        if len(data)>1:
            cores=(float(data[-1]['cpu_seconds'])-float(data[0]['cpu_seconds']))/(data[-1]['t']-data[0]['t'])
            logical=int(data[-1]['logical_processors'])
            cpu['game']=dict(cpu_core_equivalents=cores,host_cpu_percent=cores/logical*100,logical_processors=logical)
    gpu = {}
    if gpu_log.exists():
        data = []
        for row in csv.reader(gpu_log.read_text(encoding='utf-8-sig', errors='replace').splitlines()):
            try:
                timestamp=dt.datetime.strptime(row[0].strip(),'%Y/%m/%d %H:%M:%S.%f').astimezone()
                t=(timestamp-origin).total_seconds()
                values=[float(x.strip()) for x in row[1:]]
                if 10<=t<=float(last['t']):data.append(values)
            except (ValueError,IndexError):
                pass
        if data:
            for index,name in enumerate(('gpu_utilization_percent','decoder_utilization_percent','encoder_utilization_percent','used_mib')):
                values=[r[index] for r in data]
                gpu[name]=dict(min=min(values),max=max(values),mean=sum(values)/len(values),samples=len(values))
    numeric = 'FAIL' if failures else 'PASS'
    return dict(status='FAIL' if failures else 'NOT_RUN' if windows else 'PASS',numeric_status=numeric,
                visual_status='NOT_RUN' if windows else 'ENV_BLOCKED',failures=failures,codec_case=execution['codec_case'],
                mode=mode,source_fps=fps,fps_limit=limit,seconds=duration,first_t=float(first['t']),last_t=float(last['t']),
                decode_fps=decoded/(16*duration),display_fps=displayed/(16*duration) if windows else None,
                dropped=max(int(r['dropped']) for r in aggregate),errors=max(int(r['errors']) for r in aggregate),
                reconnects=max(int(r['reconnects']) for r in aggregate),superseded=superseded,
                replacement_rate=superseded/max(1,decoded) if windows else None,
                raw_replacement_rate=superseded/max(1,decoded),
                worst_channel_p95_ms=max(r['age']['p95_ms'] for r in channels) if windows else None,
                age_limit_ms=age_limit if windows else None,decoder_counts=counts,channels=channels,
                first_all_playing_upper_seconds=max(r['first_playing_upper_seconds'] or 1e99 for r in channels),
                cpu=cpu,gpu=gpu,source_cpp=execution['source_cpp'],
                scope='Windows 렌더링·PNG 직접 검수 대기' if windows else 'Linux nullrhi 디코딩만 판정. 표시·age·대체율·색·PNG는 미검증.',
                method='FPS·age·대체는 처음10초 제외. 손실/오류/재연결은 시작 구간을 포함해 보존. 첫 Playing은1초 CSV 관측 상한. GPU는 보드 전체, 송출기는 파일 복사만 사용.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder',type=Path)
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--gpu-log',type=Path,required=True)
    parser.add_argument('--cpu-log',type=Path,required=True)
    parser.add_argument('--expected-hw',type=int,default=4)
    args=parser.parse_args()
    value=analyze(args.folder,args.log,args.gpu_log,args.cpu_log,args.expected_hw)
    (args.folder/'codec-analysis.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in value.items() if k not in ('channels','gpu','cpu')},ensure_ascii=False,indent=2))
