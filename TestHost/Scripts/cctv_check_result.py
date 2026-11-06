#!/usr/bin/env python3
"""점검 JSON을 확인해 자동화에 사용할 종료 상태를 반환한다."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--native-exit-code", type=int)
    args = parser.parse_args()
    codes = {"PASS": 0, "WARN": 2, "FAIL": 1}
    try:
        value = json.loads(args.report.read_text(encoding="utf-8-sig"))
        if value.get("schema_version") != 1 or value.get("verdict") not in codes:
            raise ValueError()
        channels = value.get("channels")
        if not isinstance(channels, list) or len(channels) > 16:
            raise ValueError()
        statuses = [channel["verdict"] for channel in channels]
        if any(status not in codes for status in statuses):
            raise ValueError()
        if value.get("startup_error") or "FAIL" in statuses:
            expected = "FAIL"
        elif not statuses:
            raise ValueError()
        else:
            expected = "WARN" if "WARN" in statuses else "PASS"
        if expected != value["verdict"]:
            raise ValueError()
        code = codes[expected]
        if args.native_exit_code is not None and args.native_exit_code not in (0, code):
            print("점검 프로세스의 비정상 종료를 확인했습니다.")
            return 1
        print("CCTV 점검 판정 " + expected)
        return code
    except (OSError, ValueError, KeyError, TypeError):
        print("점검 보고서를 읽거나 판정의 일관성을 확인하지 못했습니다.")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
