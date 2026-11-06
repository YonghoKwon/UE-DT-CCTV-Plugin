"""승인된 RPM으로 이 작업의 시험 네트워크 컨테이너만 새로 만든다."""
from pathlib import Path
import argparse,json,re,time,uuid
from network_fault_test import docker,now,write_json,validate_rpm_cache

def prepare(report):
    config=json.loads((report/'configuration.json').read_text(encoding='utf-8-sig'))
    old=json.loads((report/'network-owner.json').read_text(encoding='utf-8-sig'))
    cid=old['id']
    if not re.fullmatch('[0-9a-f]{64}',cid):raise ValueError('소유 ID 형식 오류')
    inspected=docker('inspect',cid)
    if inspected.returncode==0:
        item=json.loads(inspected.stdout)[0]
        if item['Config']['Labels'].get('dtcctv.task')!='CCTV-01K-rpm-retest':raise ValueError('소유 라벨 불일치')
        active=docker('ps','-q','--no-trunc')
        ids=active.stdout.split()
        if ids:
            others=json.loads(docker('inspect',*ids).stdout)
            if any(x['HostConfig']['NetworkMode']=='container:'+cid for x in others):raise RuntimeError('이 네트워크를 쓰는 플레이어가 살아 있어 교체하지 않음')
        removed=docker('rm','-f',cid)
        if removed.returncode:raise RuntimeError('이전 소유 도구 컨테이너 정리 실패')
    old['removed']=True
    history=report/'network-owner-history';history.mkdir(exist_ok=True)
    write_json(history/(cid+'.json'),old)
    cache=validate_rpm_cache(Path(config['rpm_dir']))
    marker='ready-'+uuid.uuid4().hex
    args=['create','--pull=never','--user','0:0','--cap-add','NET_ADMIN','--security-opt','no-new-privileges',
          '--publish','127.0.0.1:18554:18554','--publish','127.0.0.1:30000-30127:30000-30127/udp',
          '--label','dtcctv.task=CCTV-01K-rpm-retest','--mount',f'type=bind,source={cache},target=/rpms,readonly',
          '--mount',f'type=bind,source={report},target=/reports']
    scripts=Path(__file__).resolve().parent
    for name,target in [('netem_runtime.js','/runtime.js'),('netem_rpm_setup.sh','/setup.sh'),('netem_control.sh','/control.sh'),('netem_probe.sh','/probe.sh')]:
        args+=['--mount',f'type=bind,source={scripts/name},target={target},readonly']
    args+=['--entrypoint','/bin/sh',old['image'],'-c',f'DTCCTV_NETEM_OWNED=1 /bin/sh /setup.sh && touch /reports/{marker} && exec sleep 14400']
    made=docker(*args);new=made.stdout.strip()
    if made.returncode or not re.fullmatch('[0-9a-f]{64}',new):raise RuntimeError('새 소유 도구 생성 실패: '+made.stderr)
    owner=dict(id=new,image=old['image'],created_at=now(),scope='조건별 독립 시험 네트워크·승인 RPM·실제 UDP 중계',cap_add=['NET_ADMIN'],network='bridge',published=['127.0.0.1:18554/tcp','127.0.0.1:30000-30127/udp'],ready_marker=marker)
    write_json(report/'network-owner.json',owner);(report/'network-owner.cid').write_text(new,encoding='ascii')
    try:
        started=docker('start',new)
        if started.returncode:raise RuntimeError('새 도구 시작 실패')
        deadline=time.monotonic()+30
        while not (report/marker).exists():
            if time.monotonic()>deadline:raise RuntimeError('서명 검증·RPM 설치 준비 제한 시간 초과')
            time.sleep(.1)
        (history/(new+'.setup.log')).write_text(docker('logs',new).stdout,encoding='utf-8')
        print(json.dumps(dict(status='READY',id=new),ensure_ascii=False))
    except Exception:
        docker('rm','-f',new)
        owner['removed']=True;write_json(report/'network-owner.json',owner)
        raise
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('report',type=Path);args=parser.parse_args();prepare(args.report.resolve())
