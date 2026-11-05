#!/usr/bin/env python3
"""소유 PID·생성시각을 검증한 뒤 스레드 시작 주소와 모듈을 읽기 전용으로 수집한다."""
import argparse
import collections
import ctypes
import ctypes.wintypes as w
import datetime as dt
import json
import os
import time
from pathlib import Path
import psutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("owner", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--interval", type=float, default=300)
    parser.add_argument("--initial-delay", type=float, default=10)
    parser.add_argument("--periodic-interval", type=float, default=300)
    args = parser.parse_args()
    record = json.loads(args.owner.read_text(encoding="utf-8-sig"))
    process = psutil.Process(record["pid"])
    expected = dt.datetime.fromisoformat(record["started_utc"].replace("Z", "+00:00")).timestamp()
    if abs(process.create_time()-expected) >= .05:
        raise RuntimeError("소유 PID 생성시각 불일치")
    kernel, psapi, ntdll = ctypes.WinDLL("kernel32", use_last_error=True), ctypes.WinDLL("psapi"), ctypes.WinDLL("ntdll")
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    ntdll.NtQueryInformationThread.argtypes = [w.HANDLE, w.ULONG, ctypes.c_void_p, w.ULONG, ctypes.c_void_p]
    ntdll.NtQueryInformationThread.restype = ctypes.c_long

    class ModuleInfo(ctypes.Structure):
        _fields_ = [("base", ctypes.c_void_p), ("size", w.DWORD), ("entry", ctypes.c_void_p)]

    psapi.EnumProcessModulesEx.argtypes = [w.HANDLE, ctypes.POINTER(w.HMODULE), w.DWORD, ctypes.POINTER(w.DWORD), w.DWORD]
    psapi.GetModuleInformation.argtypes = [w.HANDLE, w.HMODULE, ctypes.POINTER(ModuleInfo), w.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD]
    handle = kernel.OpenProcess(0x0410, False, process.pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    previous = {}
    next_sample = time.monotonic()+max(0, args.initial_delay)
    next_periodic = next_sample
    try:
        with args.output.open("a", encoding="utf-8", buffering=1) as stream, args.output.with_suffix(".5min.jsonl").open("a", encoding="utf-8", buffering=1) as periodic:
            while process.is_running() and abs(process.create_time()-expected) < .05:
                if time.monotonic() < next_sample:
                    time.sleep(max(0, min(1, next_sample-time.monotonic())))
                    continue
                array, needed = (w.HMODULE*2048)(), w.DWORD()
                if not psapi.EnumProcessModulesEx(handle, array, ctypes.sizeof(array), ctypes.byref(needed), 3):
                    raise ctypes.WinError(ctypes.get_last_error())
                mapping = []
                for module in array[:min(len(array), needed.value//ctypes.sizeof(w.HMODULE))]:
                    info, name = ModuleInfo(), ctypes.create_unicode_buffer(32768)
                    if psapi.GetModuleInformation(handle, module, ctypes.byref(info), ctypes.sizeof(info)):
                        psapi.GetModuleFileNameExW(handle, module, name, len(name))
                        mapping.append((int(info.base), int(info.base)+info.size, Path(name.value).name.lower()))
                current = {}
                for thread in process.threads():
                    thread_handle = kernel.OpenThread(0x0840, False, thread.id)
                    address, status = ctypes.c_void_p(), None
                    if thread_handle:
                        try:
                            status = ntdll.NtQueryInformationThread(thread_handle, 9, ctypes.byref(address), ctypes.sizeof(address), None)
                        finally:
                            kernel.CloseHandle(thread_handle)
                    value = int(address.value or 0)
                    current[thread.id] = dict(tid=thread.id, start=hex(value), module=next((n for a,b,n in mapping if a <= value < b), "(unknown)"), query_status=status)
                sample = dict(utc=dt.datetime.now(dt.timezone.utc).isoformat(), pid=process.pid, collector_pid=os.getpid(),
                              interval_seconds=args.interval, total=len(current), counts=dict(collections.Counter(t["module"] for t in current.values())),
                              added=[current[t] for t in current.keys()-previous.keys()],
                              removed=[previous[t] for t in previous.keys()-current.keys()], threads=list(current.values()))
                stream.write(json.dumps(sample, ensure_ascii=False)+"\n")
                if time.monotonic() >= next_periodic:
                    periodic.write(json.dumps(sample, ensure_ascii=False)+"\n")
                    next_periodic += max(1, args.periodic_interval)
                previous = current
                next_sample += max(.1, args.interval)
    except psutil.NoSuchProcess:
        pass
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    main()
