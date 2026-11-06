#!/usr/bin/env python3
"""CCTV-01L 원자료와 경계 재측정 결과를 한국어 표로 갱신한다."""
from pathlib import Path
import argparse
import csv
import datetime as dt
import json
import subprocess

ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'Saved/Reports/CCTV-01L'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def build(final=False):
    c=read(P/'checkpoint.json')
    results=[]
    for p in sorted((P/'measurements').glob('*/capacity-analysis.json')):
        a=read(p)
        visual=p.parent/'visual-review.json'
        if a['mode']=='Windows' and visual.exists():
            a['visual_review']=read(visual);a['visual_status']=a['visual_review']['status']
            a['status']='PASS' if a['numeric_status']=='PASS' and a['visual_status']=='PASS' else 'FAIL'
        results.append(a)
    boundaries=[]
    for codec in c['priority']:
        for mode in ['Windows','LinuxSW','LinuxCUDA']:
            p=P/'boundaries'/(codec+'-'+mode+'.json')
            b=read(p) if p.exists() else {'codec':codec,'mode':mode,'complete':False,'confirmed_max_channels':None,'status':'NOT_RUN','note':'우선순위에 따른 실행 대기' if not final else '4시간 예산 내 완료하지 못함. 상위 우선순위 측정을 먼저 수행.'}
            values=[a for a in results if a['codec_case']==codec and a['mode']==mode]
            maximum=b.get('confirmed_max_channels')
            evidence=[a for a in values if a['count']==maximum and a['numeric_status']=='PASS']
            if maximum:
                b['boundary_tags']=[a['tag'] for a in evidence]
                if mode=='Windows':
                    b['visual_status']='PASS' if len(evidence)>=2 and all(a.get('visual_status')=='PASS' for a in evidence) else 'NOT_RUN'
                    b['status']='PASS' if b['visual_status']=='PASS' else 'REVIEW_REQUIRED'
                b['scope']='렌더링 포함' if mode=='Windows' else 'nullrhi 디코딩만. 표시·age·대체·색 미검증.'
            b['measurements']=len(values)
            if values:
                lows=[a.get('min_channel_decode_fps') for a in values if a.get('min_channel_decode_fps') is not None]
                b['min_observed_channel_decode_fps']=min(lows) if lows else None
            boundaries.append(b)
    status='IN_PROGRESS' if not final else 'BLOCKED_BY_OTHER_WORK' if c['status']=='BLOCKED_BY_OTHER_WORK' else 'FAIL' if any(b['status']=='FAIL' for b in boundaries) else 'INCOMPLETE' if any(b['status']!='PASS' for b in boundaries) else 'PASS'
    columns=['codec','mode','count','tag','status','numeric_status','visual_status','seconds','decode_fps','min_channel_decode_fps','display_fps','min_channel_display_fps','worst_channel_p95_ms','replacement_rate','dropped','errors','reconnects','cpu_percent','gpu_percent','nvdec_percent','rss_peak_mib']
    with (P/'comparison.csv').open('w',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fieldnames=columns);w.writeheader()
        for a in results:
            row={k:a.get(k) for k in columns};row['codec']=a['codec_case']
            row['cpu_percent']=a.get('cpu',{}).get('game',{}).get('host_cpu_percent')
            row['gpu_percent']=a.get('gpu',{}).get('gpu_utilization_percent',{}).get('mean')
            row['nvdec_percent']=a.get('gpu',{}).get('decoder_utilization_percent',{}).get('mean')
            row['rss_peak_mib']=a.get('memory',{}).get('rss_mib_peak');w.writerow(row)
    head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    limits=['Windows는 게임60fps·1920×1080 오프스크린, 타이머 옵션 기본 꺼짐. 표시 FPS는 렌더 명령 등록이며 실제 모니터 표시나 종단 간 지연 아님.',
            'Linux 두 경로는 nullrhi 디코딩 한계만 판정. 표시 FPS·frame age·대체율·색·PNG를 PASS로 간주하지 않음.',
            '32 이상은 32채널 두 번 통과를 뜻하며 34~64채널이나 타 장비 성능을 검증한 표현이 아님.',
            '로컬 파일 반복 송출·단일 PC·고정 창 크기/자동 격자 기준. 채널 증가 시 타일 크기도 줄어 변환·업로드 비용이 달라짐.',
            '짧은 채널 한계 측정은 60분 소크·실제 카메라·실제 Pixel Streaming 동시 인코딩의 증거를 대체하지 않음. 이번 사용자 요청에 따라 최종 코덱 회귀로 수행.']
    result={'task_id':'CCTV-01L','status':status,'phase':'COMPLETE' if final else 'IN_PROGRESS','updated_at':dt.datetime.now().astimezone().isoformat(),'base_commit':c['base_commit'],'source_cpp':c['source_cpp'],'head_commit':head,'hardware':c['hardware'],'gates':c['gates'],'boundaries':boundaries,'measurements':results,'known_limits':limits,'decisions_needed':[]}
    write(P/'result.json',result)
    lines=['# CCTV-01L 최종 코덱별 채널 수 시험','',f'현재 판정 **{status}**. 최신 코드와 타이머 옵션을 포함한 새 빌드·새 패키지를 사용했습니다. 타이머 옵션은 기본 꺼짐입니다.','',
           f'C++ `{c["source_cpp"]}`, 시험 시작 HEAD `{c["base_commit"]}`, 보고 HEAD `{head}`. 제품 C++와 TestHost C++ 변경 없음.','',
           'Ryzen 7 7800X3D 8코어16스레드·RTX4070Ti 12GB·RAM64GB. Windows 오프스크린1920×1080·게임60fps, Linux RHEL8 SW nullrhi와 자동 CUDA최대4+SW nullrhi입니다.','',
           '초기10초 제외80초 이상, 원본97%는 채널별 최저 FPS 기준, 손실·재연결·오류0, 전 채널 Playing. Windows p95는30fps에서70ms·60fps에서50ms, 대체율1% 이하. 경계값은 두 번 통과해야 인정합니다.','',
           '|코덱|Windows HW4+SW|Linux SW|Linux 자동|','|---|---|---|---|']
    for codec in c['priority']:
        row=[]
        for mode in ['Windows','LinuxSW','LinuxCUDA']:
            b=next(x for x in boundaries if x['codec']==codec and x['mode']==mode)
            n=b.get('confirmed_max_channels')
            label='32 이상' if n==32 else str(n)+'채널' if n else '통과값 없음' if n==0 else 'NOT_RUN'
            if b['status']!='PASS':label+=' ('+b['status']+')'
            row.append(label)
        lines.append('|'+codec+'|'+'|'.join(row)+'|')
    lines+=['','## 근거와 미수행','',
            '조합별 CSV·PNG·GPU/NVDEC·CPU/RSS·컨테이너 생성/정리·실행 로그는 `measurements/`, 종합 수치는 `comparison.csv`, 상세 경계 및 실패 이력은 `result.json`에 있습니다. 중간 보고의 Windows REVIEW_REQUIRED는 수치 통과 후 직접 PNG 검수가 남았다는 뜻입니다.','']+['- '+x for x in limits]+['','미측정은 NOT_RUN, 실제 미달은 FAIL로 유지합니다. 원인 및 병목은 해당 실패 경계의 자원 수치로 구분하며 성능 기준과 기본값은 변경하지 않습니다.','',
            '## 결정 필요','', '추가 결정 없음. 운영 서버에서는01J 점검 도구로 다시 검증해야 합니다. push·PR·merge 없음.']
    (P/'RESULT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--final',action='store_true');a=p.parse_args();r=build(a.final)
    print(json.dumps({'status':r['status'],'measurements':len(r['measurements']),'boundaries':sum(b['complete'] for b in r['boundaries'])},ensure_ascii=False))
