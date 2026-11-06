#!/usr/bin/env python3
"""직접 시작한 Linux 게임의 CPU와 생성 식별자를 기록하고 종료 코드를 전달한다."""
import argparse
import csv
import datetime as dt
import json
import os
import signal
import subprocess
import time
from pathlib import Path


def process_stat(pid):
    text = Path('/proc/{}/stat'.format(pid)).read_text()
    fields = text[text.rfind(')')+2:].split()
    return int(fields[19]), (int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'), int(fields[17])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[0] == '--' else args.command
    process = subprocess.Popen(command)
    start, _, _ = process_stat(process.pid)
    args.out.with_suffix('.process.json').write_text(json.dumps(dict(pid=process.pid,start_ticks=start,path=command[0])),encoding='utf-8')
    def stop_signal(*unused):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, stop_signal)
    try:
        with args.out.open('w',encoding='utf-8',newline='',buffering=1) as stream:
            writer=csv.DictWriter(stream,['utc','pid','cpu_seconds','threads','logical_processors'])
            writer.writeheader()
            while process.poll() is None:
                try:
                    current,cpu,threads=process_stat(process.pid)
                    if current != start:
                        raise RuntimeError('게임 PID 생성 식별자 불일치')
                    writer.writerow(dict(utc=dt.datetime.now(dt.timezone.utc).isoformat(),pid=process.pid,cpu_seconds=cpu,
                                         threads=threads,logical_processors=os.cpu_count()))
                    stream.flush()
                except FileNotFoundError:
                    break
                time.sleep(1)
        return process.wait()
    finally:
        if process.poll() is None:
            try:
                current,_,_=process_stat(process.pid)
                if current==start:
                    process.terminate()
                    try:process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        if process_stat(process.pid)[0]==start:process.kill()
                        process.wait()
            except FileNotFoundError:
                pass


if __name__=='__main__':
    raise SystemExit(main())
