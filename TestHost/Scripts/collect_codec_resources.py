#!/usr/bin/env python3
"""소유 PID·생성시각·경로를 확인해 코덱 시험의 CPU 사용량을 수집한다."""
import argparse
import csv
import datetime as dt
import json
import time
from pathlib import Path
import psutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('owners', type=Path)
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    owners = json.loads(args.owners.read_text(encoding='utf-8-sig'))
    entries = []
    for item in owners:
        record = json.loads(Path(item['record']).read_text(encoding='utf-8-sig'))
        expected = dt.datetime.fromisoformat(record['started_utc'].replace('Z', '+00:00')).timestamp()
        process = psutil.Process(record['pid'])
        if abs(process.create_time()-expected) >= .05 or Path(process.exe()) != Path(record['path']):
            raise RuntimeError('자원 수집 대상 소유권 불일치')
        entries.append((item['role'], process, expected))
    with args.out.open('w', encoding='utf-8', newline='', buffering=1) as stream:
        writer = csv.DictWriter(stream, ['utc','role','pid','cpu_seconds','rss_bytes','threads'])
        writer.writeheader()
        while True:
            life = False
            for role, process, expected in entries:
                try:
                    if not process.is_running() or abs(process.create_time()-expected) >= .05:
                        continue
                    if role in ('game','lifetime'):
                        life = True
                    cpu = process.cpu_times()
                    writer.writerow(dict(utc=dt.datetime.now(dt.timezone.utc).isoformat(), role=role, pid=process.pid,
                                         cpu_seconds=cpu.user+cpu.system, rss_bytes=process.memory_info().rss,
                                         threads=process.num_threads()))
                except psutil.NoSuchProcess:
                    pass
            stream.flush()
            if not life:
                break
            time.sleep(1)


if __name__ == '__main__':
    main()
