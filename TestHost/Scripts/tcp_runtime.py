#!/usr/bin/env python3
"""새 시험 컨테이너 안에서만 수신기와 소켓 상태를 관측한다."""
import argparse, datetime as dt, json, os, signal, subprocess as sp, time
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('mode',choices=['plugin','plugin-auto','copy','decode']);ap.add_argument('--count',type=int,required=True)
    ap.add_argument('--seconds',type=int,default=160);ap.add_argument('--option');ap.add_argument('--buffer',type=int,default=0);args=ap.parse_args()
    out=Path('/reports');out.joinpath('runtime-environment.json').write_text(json.dumps(dict(
        rmem_max=Path('/proc/sys/net/core/rmem_max').read_text() if Path('/proc/sys/net/core/rmem_max').exists() else '네임스페이스에 노출되지 않음',
        tcp_rmem=Path('/proc/sys/net/ipv4/tcp_rmem').read_text(),meminfo=Path('/proc/meminfo').read_text(),
        sysctl_changed=bool(os.environ.get('DTCCTV_NAMESPACE_RMEM_DEFAULT_BYTES')),namespace_tcp_rmem_default_bytes=int(os.environ.get('DTCCTV_NAMESPACE_RMEM_DEFAULT_BYTES','0'))),ensure_ascii=False,indent=2),encoding='utf-8')
    children=[];logs=[];records=[]
    def identity(pid):
        text=Path('/proc/{}/stat'.format(pid)).read_text();return text[text.rfind(')')+2:].split()[19]
    def stop(*unused):raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM,stop)
    try:
        if args.mode.startswith('plugin'):
            channels=[dict(url='rtsp://host.docker.internal:8554/cam',transport='tcp',software_only=args.mode=='plugin') for _ in range(args.count)]
            (out/'input.json').write_text(json.dumps(dict(schema_version=1,channels=channels)))
            commands=[['/package/DTCctvTestHost/Binaries/Linux/DTCctvTestHost','-nullrhi','-unattended','-nosplash','-nosound','-userdir=/reports/userdir',
                '-CctvCheckList=/reports/input.json','-CctvCheckSeconds='+str(args.seconds),'-ExecCmds=t.MaxFPS 60','-abslog=/reports/game.log']]
            if args.mode=='plugin':commands[0].append('-CctvSoftware')
        else:
            commands=[]
            for n in range(args.count):
                command=['/usr/libexec/platform-python','/check/tcp_receiver.py','--folder','/package/DTCctvTestHost/Binaries/Linux','--out','/reports/receiver-{}.jsonl'.format(n),
                    '--mode',args.mode,'--url','rtsp://host.docker.internal:8554/cam','--seconds',str(args.seconds)]
                if args.option:command+=['--option',args.option,'--buffer',str(args.buffer)]
                commands.append(command)
        for n,command in enumerate(commands):
            log=(out/'child-{}.log'.format(n)).open('w');logs.append(log)
            child=sp.Popen(command,stdout=log,stderr=sp.STDOUT);children.append(child)
            records.append(dict(pid=child.pid,start_ticks=identity(child.pid),command=command))
        (out/'linux-owned-processes.json').write_text(json.dumps(records,indent=2))
        began=time.monotonic()
        with (out/'resources.jsonl').open('w',buffering=1) as stream:
            while any(child.poll() is None for child in children):
                stamp=time.monotonic();rows=[]
                for child,record in zip(children,records):
                    if child.poll() is not None:continue
                    try:
                        raw=Path('/proc/{}/stat'.format(child.pid)).read_text();f=raw[raw.rfind(')')+2:].split()
                        if f[19]!=record['start_ticks']:raise RuntimeError('소유 PID 생성 식별자 변경')
                        status=Path('/proc/{}/status'.format(child.pid)).read_text()
                        wchan={}
                        for t in Path('/proc/{}/task'.format(child.pid)).iterdir():
                            name=(t/'comm').read_text().strip()
                            if name in ('DTCctvRead','DTCctvDecode'):
                                try:wchan[t.name]=dict(name=name,wchan=(t/'wchan').read_text().strip())
                                except FileNotFoundError:pass
                        rows.append(dict(pid=child.pid,cpu_ticks=int(f[11])+int(f[12]),rss_pages=int(f[21]),threads=int(f[17]),wchan=wchan))
                    except FileNotFoundError:pass
                stream.write(json.dumps(dict(t=stamp-began,utc=dt.datetime.utcnow().isoformat()+'Z',processes=rows))+'\n')
                if stamp-began>args.seconds+55:raise RuntimeError('소유 수신기 종료 제한 초과')
                time.sleep(1)
        result=dict(status='MEASURED',exit_codes=[child.returncode for child in children],elapsed_seconds=time.monotonic()-began,clock_ticks=os.sysconf('SC_CLK_TCK'),page_size=os.sysconf('SC_PAGE_SIZE'))
        (out/'runtime-result.json').write_text(json.dumps(result,indent=2))
        # 점검 화면의1/2는 FAIL/WARN 판정 코드다. 원시 코드와 판정은 보고서에서 별도로 보존한다.
        return 0 if all(child.returncode==0 or (args.mode.startswith('plugin') and child.returncode in (1,2)) for child in children) else 1
    finally:
        for child,record in zip(children,records):
            if child.poll() is None:
                try:
                    if identity(child.pid)!=record['start_ticks']:raise RuntimeError('정리할 PID 소유권 불일치')
                    child.terminate()
                    try:child.wait(timeout=5)
                    except sp.TimeoutExpired:
                        if identity(child.pid)==record['start_ticks']:child.kill()
                        child.wait()
                except FileNotFoundError:pass
        for log in logs:log.close()
if __name__=='__main__':raise SystemExit(main())
