#!/usr/bin/env python3
"""소유 프로세스의 핸들 종류와 객체 식별자만 읽는다. 이름·경로·내용은 수집하지 않는다."""
import argparse
import collections
import ctypes as c
import ctypes.wintypes as w
import datetime as dt
import json
import time
from pathlib import Path

import numpy as np
import psutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('owner', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--interval', type=float, default=1)
    parser.add_argument('--max-samples', type=int, default=0)
    args = parser.parse_args()
    owner = json.loads(args.owner.read_text(encoding='utf-8-sig'))
    process = psutil.Process(owner['pid'])
    expected = dt.datetime.fromisoformat(owner['started_utc'].replace('Z', '+00:00')).timestamp()
    if abs(process.create_time()-expected) >= .05 or Path(process.exe()) != Path(owner['path']):
        raise RuntimeError('소유 PID·생성시각·경로 불일치')
    kernel, nt = c.WinDLL('kernel32', use_last_error=True), c.WinDLL('ntdll')
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.DuplicateHandle.argtypes = [w.HANDLE, w.HANDLE, w.HANDLE, c.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD]
    nt.NtQuerySystemInformation.argtypes = [w.ULONG, c.c_void_p, w.ULONG, c.POINTER(w.ULONG)]
    nt.NtQuerySystemInformation.restype = c.c_long
    nt.NtQueryObject.argtypes = [w.HANDLE, w.ULONG, c.c_void_p, w.ULONG, c.POINTER(w.ULONG)]
    nt.NtQueryObject.restype = c.c_long

    class UnicodeString(c.Structure):
        _fields_ = [('length', w.USHORT), ('maximum', w.USHORT), ('buffer', c.c_void_p)]

    class ObjectTypeInfo(c.Structure):
        _fields_ = [('name', UnicodeString), ('counters', w.ULONG*12), ('invalid_attributes', w.ULONG),
                    ('generic_mapping', w.ULONG*4), ('valid_access', w.ULONG),
                    ('security', c.c_ubyte), ('maintain', c.c_ubyte), ('index', c.c_ubyte), ('reserved', c.c_ubyte),
                    ('pool_type', w.ULONG), ('paged_charge', w.ULONG), ('nonpaged_charge', w.ULONG)]

    dtype = np.dtype([('object', '<u8'), ('pid', '<u8'), ('handle', '<u8'), ('access', '<u4'),
                      ('trace', '<u2'), ('type', '<u2'), ('attributes', '<u4'), ('reserved', '<u4')])
    target = kernel.OpenProcess(0x1040, False, process.pid)
    if not target:
        raise c.WinError(c.get_last_error())
    names, previous = {}, {}
    collected = 0
    # EtwRegistration 같은 종류는 개별 핸들 복제가 거부될 수 있어 시스템 종류 표를 먼저 읽는다.
    type_buffer, type_needed = c.create_string_buffer(65536), w.ULONG()
    if nt.NtQueryObject(None, 3, type_buffer, len(type_buffer), c.byref(type_needed)) == 0:
        type_count = w.ULONG.from_buffer(type_buffer).value
        address = c.addressof(type_buffer)+8
        limit = c.addressof(type_buffer)+len(type_buffer)
        for _ in range(type_count):
            if address+c.sizeof(ObjectTypeInfo) > limit:
                raise RuntimeError('시스템 객체 종류 표 범위 오류')
            info = ObjectTypeInfo.from_address(address)
            if not info.name.buffer or not (address <= info.name.buffer <= limit-info.name.maximum):
                raise RuntimeError('시스템 객체 종류 이름 범위 오류')
            names[info.index] = c.wstring_at(info.name.buffer, info.name.length//2)
            address = (info.name.buffer+info.name.maximum+7) & ~7
    size, next_sample = 8*1024*1024, time.monotonic()
    try:
        with args.output.open('a', encoding='utf-8', buffering=1) as stream:
            while process.is_running() and abs(process.create_time()-expected) < .05:
                if time.monotonic() < next_sample:
                    time.sleep(min(.2, next_sample-time.monotonic()))
                    continue
                began = time.monotonic()
                while True:
                    buffer, needed = c.create_string_buffer(size), w.ULONG()
                    status = nt.NtQuerySystemInformation(64, buffer, size, c.byref(needed))
                    if status == 0:
                        break
                    if status & 0xffffffff != 0xc0000004:
                        raise RuntimeError(f'핸들 표 조회 실패: {status & 0xffffffff:08x}')
                    size = max(size*2, needed.value+65536)
                count = c.c_size_t.from_buffer(buffer).value
                entries = np.frombuffer(buffer, dtype=dtype, count=count, offset=16)
                own = entries[entries['pid'] == process.pid]
                current = {}
                for row in own:
                    index, value = int(row['type']), int(row['handle'])
                    if index not in names:
                        duplicate, length = w.HANDLE(), w.ULONG()
                        if kernel.DuplicateHandle(target, value, kernel.GetCurrentProcess(), c.byref(duplicate), 0, False, 2):
                            try:
                                info = c.create_string_buffer(4096)
                                if nt.NtQueryObject(duplicate, 2, info, len(info), c.byref(length)) == 0:
                                    name = UnicodeString.from_buffer(info)
                                    names[index] = c.wstring_at(name.buffer, name.length//2)
                            finally:
                                kernel.CloseHandle(duplicate)
                    item = dict(handle=hex(value), object=hex(int(row['object'])), type=names.get(index, f'미확인_{index}'))
                    current[(value, int(row['object']))] = item
                sample = dict(utc=dt.datetime.now(dt.timezone.utc).isoformat(), pid=process.pid,
                              total=len(current), counts=dict(collections.Counter(r['type'] for r in current.values())),
                              collection_seconds=time.monotonic()-began,
                              added=[current[k] for k in current.keys()-previous.keys()],
                              removed=[previous[k] for k in previous.keys()-current.keys()], handles=list(current.values()))
                stream.write(json.dumps(sample, ensure_ascii=False)+'\n')
                previous = current
                collected += 1
                if args.max_samples and collected >= args.max_samples:
                    break
                next_sample += max(.25, args.interval)
    except psutil.NoSuchProcess:
        pass
    finally:
        kernel.CloseHandle(target)


if __name__ == '__main__':
    main()
