#!/usr/bin/env python3
"""표준 라이브러리만 사용하는 로그 가림·결과 분석 도우미."""
import sys, os, re, json, csv, struct, zlib, platform
from pathlib import Path

def redact(line):
    secret = os.environ.get("DTCCTV_CHECK_URL", "")
    if secret:
        line = line.replace(secret, "rtsp://***")
    line = re.sub(r"(?i)rtsps?://[^\s\"<>]+", "rtsp://***", line)
    return re.sub(r"(?i)(Authorization\s*:\s*)([^\r\n]+)", r"\1***", line)

def png_tiles(path):
    b = path.read_bytes()
    if b[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("PNG 서명 오류")
    pos, compressed = 8, b""
    while pos < len(b):
        n = struct.unpack(">I", b[pos:pos+4])[0]
        typ, chunk = b[pos+4:pos+8], b[pos+8:pos+8+n]
        if typ == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        if typ == b"IDAT":
            compressed += chunk
        pos += n+12
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError("지원하지 않는 PNG 픽셀 형식")
    channels = 3 if color == 2 else 4
    stride = width*channels
    data = zlib.decompress(compressed)
    rows, prev, offset = [], bytearray(stride), 0
    for _ in range(height):
        filt = data[offset]
        cur = bytearray(data[offset+1:offset+1+stride])
        offset += stride+1
        for i in range(stride):
            a = cur[i-channels] if i >= channels else 0
            c = prev[i-channels] if i >= channels else 0
            above = prev[i]
            if filt == 1:
                predictor = a
            elif filt == 2:
                predictor = above
            elif filt == 3:
                predictor = (a+above)//2
            elif filt == 4:
                p = a+above-c
                predictor = min((a, above, c), key=lambda v: abs(p-v))
            elif filt == 0:
                predictor = 0
            else:
                raise ValueError("PNG 필터 오류")
            cur[i] = (cur[i]+predictor)&255
        rows.append(cur)
        prev = cur
    non_black = []
    for ty in range(4):
        for tx in range(4):
            lit = False
            for fy in (0.25, 0.5, 0.75):
                for fx in (0.25, 0.5, 0.75):
                    x = min(width-1, int((tx+fx)*width/4))
                    y = min(height-1, int((ty+fy)*height/4))
                    lit |= any(rows[y][x*channels:x*channels+3])
            non_black.append(lit)
    return {"width": width, "height": height, "non_black_tiles": sum(non_black)}

def analyze(folder, scope, code, timed_out, expected_codec="", source_fps=60, codec_mode=False):
    result = {"status": "FAIL", "scope": scope, "exit_code": code, "timed_out": timed_out,
              "platform": platform.platform(), "known_limits": [], "failures": []}
    if scope == "software":
        result["known_limits"].append("소프트웨어 로더·디코딩 범위만 확인. Vulkan/NVDEC/렌더링 NOT_RUN.")
    elif scope == "decode":
        result["known_limits"].append("nullrhi 자동 디코딩만 확인. Vulkan·표시 FPS·age·대체율·색·PNG NOT_RUN.")
    result.update(expected_codec=expected_codec, source_fps=source_fps, codec_mode=codec_mode)
    try:
        rows = list(csv.DictReader((folder/"samples.csv").open(encoding="utf-8-sig")))
        steady = [r for r in rows if float(r["t"]) >= 10]
        first, last = steady[0], steady[-1]
        seconds = float(last["t"])-float(first["t"])
        result.update(seconds=seconds,
            decode_fps=(int(last["decoded"])-int(first["decoded"]))/(16*seconds),
            display_fps=(int(last["displayed"])-int(first["displayed"]))/(16*seconds),
            dropped=max(int(r["dropped"]) for r in rows),
            errors=max(int(r["errors"]) for r in rows),
            reconnects=max(int(r["reconnects"]) for r in rows),
            superseded=max(int(r["superseded"]) for r in rows),
            all_playing=all(int(r["playing"]) == 16 for r in steady))
        channels = list(csv.DictReader((folder/"samples.channels.csv").open(encoding="utf-8-sig")))
        stamp = max(float(r["t"]) for r in channels)
        latest = [r for r in channels if float(r["t"]) == stamp]
        result["cuda_channels"] = sum(r["decoder"] == "cuda" for r in latest)
        result["software_channels"] = sum(r["decoder"] == "software" for r in latest)
        result["channels"] = [{k: r[k] for k in ("channel", "decoder", "decode_fps", "display_fps", "decoded")} for r in latest]
        minimum_seconds = 80 if codec_mode else 60
        fps_limit = source_fps*97/100 if codec_mode else 58
        result.update(fps_limit=fps_limit,minimum_seconds=minimum_seconds)
        if code or timed_out or seconds < minimum_seconds or not result["all_playing"]:
            result["failures"].append("정상 종료·측정 시간·16 Playing 기준 미달")
        if result["decode_fps"] < fps_limit or result["dropped"] or result["reconnects"] or (codec_mode and result["errors"]):
            result["failures"].append("디코딩 FPS·손실·재연결 기준 미달")
        if codec_mode:
            per_channel=[]
            for channel in range(16):
                group=[r for r in channels if int(r["channel"])==channel and float(r["t"])>=10]
                a,b=group[0],group[-1]
                per_channel.append(dict(channel=channel,codec=b["codec"],decoder=b["decoder"],
                    decode_fps=(int(b["decoded"])-int(a["decoded"]))/ (float(b["t"])-float(a["t"])),
                    display_fps=(int(b["displayed"])-int(a["displayed"]))/ (float(b["t"])-float(a["t"]))))
            result["codec_channels"]=per_channel
            if any(r["decode_fps"]<fps_limit for r in per_channel):
                result["failures"].append("채널별 원본97% 디코딩 기준 미달")
            if expected_codec and any(r["codec"]!=expected_codec for r in per_channel):
                result["failures"].append("요청 코덱 불일치")
            if scope=="software" and result["software_channels"]!=16:
                result["failures"].append("소프트웨어16 기준 미달")
        if scope=="decode" and (result["cuda_channels"]!=4 or result["software_channels"]!=12):
            result["failures"].append("CUDA4/SW12 기준 미달")
        if scope == "gpu":
            result["screenshot"] = png_tiles(folder/"final.png")
            if result["cuda_channels"] != 4 or result["software_channels"] != 12:
                result["failures"].append("기본 CUDA4/SW12 기준 미달")
            if result["display_fps"] < fps_limit or (not codec_mode and result["superseded"]):
                result["failures"].append("표시 FPS·프레임 대체 기준 미달")
            if codec_mode:
                if any(r["display_fps"]<fps_limit for r in result["codec_channels"]):
                    result["failures"].append("채널별 원본97% 표시 기준 미달")
                decoded_delta=int(last["decoded"])-int(first["decoded"])
                superseded_delta=int(last["superseded"])-int(first["superseded"])
                if superseded_delta/max(1,decoded_delta)>.01:
                    result["failures"].append("대체율1% 기준 미달")
                age_limit=70 if source_fps==30 else 50
                if any(float(r["frame_age_p95_ms"])>age_limit for r in latest):
                    result["failures"].append("채널별 frame age p95 기준 미달")
            if result["screenshot"] != {"width": 1920, "height": 1080, "non_black_tiles": 16}:
                result["failures"].append("1920x1080·16개 비검은 영상 표본 기준 미달")
            text = (folder/"game.log").read_text(encoding="utf-8", errors="replace")
            if "LogVulkanRHI:" not in text:
                result["failures"].append("Vulkan 로그 없음")
        result["status"] = "FAIL" if result["failures"] else "PASS"
    except Exception as error:
        result["failures"].append(redact(str(error)))
    (folder/"result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(result["status"]+" | "+scope+" | "+str(folder))
    return 0 if result["status"] == "PASS" else 1

if __name__ == "__main__":
    if sys.argv[1] == "redact":
        for line in sys.stdin:
            sys.stdout.write(redact(line))
            sys.stdout.flush()
    else:
        raise SystemExit(analyze(Path(sys.argv[2]), sys.argv[3], int(sys.argv[4]), int(sys.argv[5]),
            sys.argv[6] if len(sys.argv)>6 else "", int(sys.argv[7]) if len(sys.argv)>7 else 60,
            bool(int(sys.argv[8])) if len(sys.argv)>8 else False))
