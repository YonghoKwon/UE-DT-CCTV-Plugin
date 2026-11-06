#!/usr/bin/env python3
"""실제 배포 FFmpeg 라이브러리에서 디코더·하드웨어 구성을 조회한다."""
import argparse
import ctypes as c
import hashlib
import json
import os
import platform
from pathlib import Path


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()


def inspect(folder):
    windows = os.name == 'nt'
    names = ['avutil-60.dll', 'swresample-6.dll', 'avcodec-62.dll'] if windows else [
        'libavutil.so.60', 'libswresample.so.6', 'libavcodec.so.62']
    dll_directory = os.add_dll_directory(str(folder)) if windows and hasattr(os, 'add_dll_directory') else None
    libraries = [c.CDLL(str(folder/name), mode=getattr(c, 'RTLD_GLOBAL', 0)) for name in names]
    util, _, codec = libraries
    util.av_version_info.restype = c.c_char_p
    util.av_hwdevice_iterate_types.argtypes = [c.c_int]
    util.av_hwdevice_get_type_name.argtypes = [c.c_int]
    util.av_hwdevice_get_type_name.restype = c.c_char_p
    util.av_get_pix_fmt_name.argtypes = [c.c_int]
    util.av_get_pix_fmt_name.restype = c.c_char_p
    codec.avcodec_configuration.restype = c.c_char_p
    codec.avcodec_license.restype = c.c_char_p
    codec.av_codec_iterate.argtypes = [c.POINTER(c.c_void_p)]
    codec.av_codec_iterate.restype = c.c_void_p
    codec.av_codec_is_decoder.argtypes = [c.c_void_p]
    codec.avcodec_find_decoder.argtypes = [c.c_int]
    codec.avcodec_find_decoder.restype = c.c_void_p
    codec.avcodec_descriptor_get_by_name.argtypes = [c.c_char_p]
    codec.avcodec_descriptor_get_by_name.restype = c.c_void_p

    class Codec(c.Structure):
        _fields_ = [('name', c.c_char_p), ('long_name', c.c_char_p), ('type', c.c_int),
                    ('id', c.c_int), ('capabilities', c.c_int)]

    class HwConfig(c.Structure):
        _fields_ = [('pix_fmt', c.c_int), ('methods', c.c_int), ('device_type', c.c_int)]

    codec.avcodec_get_hw_config.argtypes = [c.c_void_p, c.c_int]
    codec.avcodec_get_hw_config.restype = c.POINTER(HwConfig)
    def decode(value):
        return value.decode('utf-8', errors='replace') if value else None
    devices, value = [], 0
    while True:
        value = util.av_hwdevice_iterate_types(value)
        if not value:
            break
        devices.append(decode(util.av_hwdevice_get_type_name(value)))
    requested = {}
    for name in ('h264', 'hevc', 'mjpeg', 'mpeg4', 'vp9', 'av1'):
        descriptor = codec.avcodec_descriptor_get_by_name(name.encode())
        identifier = c.c_int.from_address(descriptor).value if descriptor else None
        default = codec.avcodec_find_decoder(identifier) if identifier is not None else None
        requested[name] = dict(codec_id=identifier, default_decoder=decode(Codec.from_address(default).name) if default else None,
                               implementations=[])
    opaque = c.c_void_p()
    while True:
        pointer = codec.av_codec_iterate(c.byref(opaque))
        if not pointer:
            break
        info = Codec.from_address(pointer)
        if not codec.av_codec_is_decoder(pointer):
            continue
        for value in requested.values():
            if value['codec_id'] != info.id:
                continue
            configs, index = [], 0
            while True:
                hw = codec.avcodec_get_hw_config(pointer, index)
                if not hw:
                    break
                cfg = hw.contents
                configs.append(dict(device=decode(util.av_hwdevice_get_type_name(cfg.device_type)),
                                    pixel_format=decode(util.av_get_pix_fmt_name(cfg.pix_fmt)), methods=cfg.methods))
                index += 1
            value['implementations'].append(dict(name=decode(info.name), description=decode(info.long_name),
                                                  capabilities=info.capabilities, hw_configs=configs))
    return dict(status='PASS' if all(v['default_decoder'] for v in requested.values()) else 'FAIL',
                scope='디코더·하드웨어 경로의 배포 라이브러리 구성 확인. 실제 디코딩 성공과 별도.',
                version=decode(util.av_version_info()), license=decode(codec.avcodec_license()),
                configuration=decode(codec.avcodec_configuration()), platform=platform.platform(),
                compiled_device_types=devices, codecs=requested,
                libraries=[dict(path=str(folder/name), sha256=digest(folder/name)) for name in names],
                hardware_decode_execution_status='NOT_RUN')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    value = inspect(args.folder.resolve())
    args.out.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(status=value['status'], version=value['version'], license=value['license'],
                         devices=value['compiled_device_types'], codecs={k:v['default_decoder'] for k,v in value['codecs'].items()}),
                     ensure_ascii=False))
