#!/usr/bin/env python3
"""컨테이너 전용 장애 시험의 조건표와 기존 이미지 준비 상태를 기록한다."""
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import sys


SPECS = {
    "N1": {"delay_ms": 50},
    "N2": {"delay_ms": 150, "jitter_ms": 30},
    "N3": {"loss_percent": 0.5},
    "N4": {"loss_percent": 2},
    "N5": {"aggregate_fraction": 0.8},
    "N6": {"loss_percent": 100, "outage_seconds": 10, "recovery_limit_seconds": 5},
    "N7": {"delay_ms": 50, "loss_percent": 0.5, "jitter_ms": 10},
}


def now():
    return dt.datetime.now().astimezone().isoformat()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def docker(*args, timeout=40):
    return subprocess.run(["docker", *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          encoding="utf-8", errors="replace", timeout=timeout)


def probe_image(image, out):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/@:-]*", image):
        raise ValueError("기존 이미지 이름/ID 형식이 올바르지 않습니다.")
    out.mkdir(parents=True, exist_ok=False)
    cid = ""
    cidfile = out / "owned.cid"
    value = dict(image=image, status="ENV_BLOCKED", exit_code=None, tc=False,
                 netem=False, ifb=False, cap_requested=["NET_ADMIN"], note="")
    try:
        script = Path(__file__).with_name("netem_probe.sh").resolve()
        created = docker("create", "--pull", "never", "--network", "none", "--read-only",
                         "--user", "0:0", "--cap-drop", "ALL", "--cap-add", "NET_ADMIN",
                         "--security-opt", "no-new-privileges", "--cidfile", str(cidfile),
                         "--label", "dtcctv.task=CCTV-01K",
                         "--mount", "type=bind,source={},target=/probe.sh,readonly".format(script),
                         "--entrypoint", "/bin/sh", image, "/probe.sh")
        (out / "create.log").write_text(created.stderr, encoding="utf-8")
        if cidfile.is_file():
            cid = cidfile.read_text(encoding="ascii").strip()
        if created.returncode or not re.fullmatch(r"[0-9a-f]{64}", cid):
            raise RuntimeError("자체 검사 컨테이너 생성 실패")
        write_json(out / "owned-container.json", dict(id=cid, image=image, created_at=now(),
                   scope="기존 이미지 도구/컨테이너 네임스페이스 검사 전용"))
        started = docker("start", "-a", cid)
        text = started.stdout + started.stderr
        (out / "probe.log").write_text(text, encoding="utf-8")
        inspected = docker("inspect", "--format", "{{.State.ExitCode}}", cid)
        value["exit_code"] = int(inspected.stdout.strip())
        value["tc"] = bool(re.search(r"^tc=/", text, re.M) or "TC_KIND_BUSYBOX" in text)
        value["netem"] = "NETEM_OK" in text
        value["ifb"] = "IFB_NETEM_OK" in text
        if not value["tc"]:
            value["note"] = "독립 tc 및 사용 가능한 BusyBox tc 없음"
        elif not value["netem"]:
            value["note"] = "tc는 있으나 컨테이너 내부 netem 사용 실패; 원시 출력 확인 필요"
        elif not value["ifb"]:
            value["note"] = "netem 가능·수신 IFB 경로 미확인"
        else:
            value.update(status="READY", note="컨테이너 내부 netem/IFB 준비 확인; UE 측정은 별도")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        value["note"] = str(error)
    finally:
        if not cid and cidfile.is_file():
            cid = cidfile.read_text(encoding="ascii").strip()
        if re.fullmatch(r"[0-9a-f]{64}", cid):
            removed = docker("rm", "-f", cid)
            (out / "cleanup.log").write_text(removed.stdout + removed.stderr, encoding="utf-8")
            if removed.returncode:
                value.update(status="FAIL", note="자체 컨테이너 정리 실패")
        write_json(out / "result.json", value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--probe", action="store_true", help="기존 로컬 이미지만 실제 검사")
    parser.add_argument("--image", action="append", help="검사할 기존 이미지; 생략하면 전체 로컬 이미지")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--aggregate-bps", type=int, help="N5용 실측 전체 스트림 비트레이트")
    args = parser.parse_args()
    if args.seconds < 120 or (args.aggregate_bps is not None and args.aggregate_bps <= 0):
        parser.error("조건별120초 이상, 전체 비트레이트는 양수여야 합니다.")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    images = []
    if args.probe:
        if args.image:
            candidates = list(dict.fromkeys(args.image))
        else:
            inventory = docker("images", "--format", "{{.ID}}")
            if inventory.returncode:
                raise RuntimeError("기존 Docker 이미지 목록 조회 실패")
            candidates = list(dict.fromkeys(inventory.stdout.split()))
        for index, image in enumerate(candidates):
            value = probe_image(image, out / "images" / "{:02d}".format(index))
            images.append(value)
            with (out / "progress.md").open("a", encoding="utf-8") as stream:
                stream.write("{} | 이미지{} | {} | {}\n".format(now(), index, value["status"], value["note"]))
    ready = any(x["status"] == "READY" for x in images)
    blocked = args.probe and not ready
    status = "ENV_BLOCKED" if blocked else "READY" if ready else "NOT_RUN"
    cases = []
    for codec in ("h264", "hevc"):
        for transport in ("tcp", "udp"):
            for condition, settings in SPECS.items():
                spec = dict(settings)
                if condition == "N5":
                    spec["rate_bps"] = args.aggregate_bps * 4 // 5 if args.aggregate_bps else None
                criteria = dict(memory_and_threads_return_to_baseline=True)
                if condition in ("N1", "N2", "N7"):
                    criteria.update(reconnects=0, decode_fps_min=29.1, screen_normal=True,
                                    adjusted_latency="네트워크 포함 지연 측정 계약 필요")
                elif condition == "N3":
                    criteria.update(playback_maintained=True, reconnects=0,
                                    tcp_retransmission_or_udp_corruption_recovery="기록 필요")
                elif condition in ("N4", "N5"):
                    criteria.update(no_crash_or_stall=True, automatic_non_repeating_recovery=True,
                                    numeric_fps_gate=None)
                else:
                    criteria.update(last_frame_retained=True, all_channels_recover_within_seconds=5,
                                    no_crash=True)
                cases.append(dict(id="{}-{}-{}".format(codec, transport, condition), codec=codec,
                    transport=transport, condition=condition, channels=16, width=1920, height=1080,
                    original_fps=30, seconds=args.seconds, warmup_seconds=10, impairment=spec,criteria=criteria,
                    status="ENV_BLOCKED" if blocked else "NOT_RUN", execution_status="NOT_RUN",
                    decode_fps=None, reconnects=None, memory_returned=None, threads_returned=None))
    value = dict(task_id="CCTV-01K", status=status, checked_at=now(), images=images, cases=cases,
        scope="이 도구는 조건표·기존 이미지 준비 검사를 수행. UE120초 통합 측정 결과가 아님.",
        injection_script="netem_control.sh", no_download=True, host_network_changed=False,
        latency_note="현재 frame age는 디코더 반환~렌더 등록. 네트워크 지연을 포함하지 않으므로50/150ms를 빼서 합격 처리할 수 없음.")
    write_json(out / "result.json", value)
    print(json.dumps(dict(status=status, images=len(images), cases=len(cases)), ensure_ascii=False))
    return 2 if blocked else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("검사 실행 실패: " + str(error), file=sys.stderr)
        sys.exit(1)
