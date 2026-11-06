#!/usr/bin/env python3
"""배포 FFmpeg 공유 라이브러리만 사용하는 복사/디코딩 대조 수신기. 새 바이너리를 받지 않는다."""
import argparse, ctypes as c, json, os, time
from pathlib import Path

class Stream(c.Structure):
    _fields_=[('av_class',c.c_void_p),('index',c.c_int),('id',c.c_int),('codecpar',c.c_void_p)]
class Format(c.Structure):
    _fields_=[('av_class',c.c_void_p),('iformat',c.c_void_p),('oformat',c.c_void_p),('priv_data',c.c_void_p),('pb',c.c_void_p),('flags',c.c_int),('count',c.c_uint),('streams',c.POINTER(c.POINTER(Stream)))]
class Packet(c.Structure):
    _fields_=[('buf',c.c_void_p),('pts',c.c_int64),('dts',c.c_int64),('data',c.c_void_p),('size',c.c_int),('stream_index',c.c_int)]
class Entry(c.Structure):
    _fields_=[('key',c.c_char_p),('value',c.c_char_p)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--folder',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--url',required=True);ap.add_argument('--mode',choices=['copy','decode'],required=True)
    ap.add_argument('--seconds',type=int,default=160);ap.add_argument('--option');ap.add_argument('--buffer',type=int,default=0)
    args=ap.parse_args()
    if args.seconds<120:ap.error('측정은120초 이상이어야 합니다.')
    names=['libavutil.so.60','libswresample.so.6','libavcodec.so.62','libavformat.so.62']
    libs=[c.CDLL(str(args.folder/name),mode=c.RTLD_GLOBAL) for name in names];u,_,co,f=libs
    def bind(lib,name,result,types):
        fun=getattr(lib,name);fun.restype=result;fun.argtypes=types;return fun
    ptr=c.c_void_p;pp=c.POINTER(ptr);i=c.c_int
    alloc=bind(f,'avformat_alloc_context',ptr,[])
    op=bind(f,'avformat_open_input',i,[pp,c.c_char_p,ptr,pp]);info=bind(f,'avformat_find_stream_info',i,[ptr,pp])
    best=bind(f,'av_find_best_stream',i,[ptr,i,i,i,pp,i]);read=bind(f,'av_read_frame',i,[ptr,ptr]);close=bind(f,'avformat_close_input',None,[pp])
    setdict=bind(u,'av_dict_set',i,[pp,c.c_char_p,c.c_char_p,i]);getdict=bind(u,'av_dict_get',c.POINTER(Entry),[ptr,c.c_char_p,ptr,i]);freedict=bind(u,'av_dict_free',None,[pp])
    packet_alloc=bind(co,'av_packet_alloc',ptr,[]);unref=bind(co,'av_packet_unref',None,[ptr]);packet_free=bind(co,'av_packet_free',None,[pp])
    ctxalloc=bind(co,'avcodec_alloc_context3',ptr,[ptr]);params=bind(co,'avcodec_parameters_to_context',i,[ptr,ptr]);codecopen=bind(co,'avcodec_open2',i,[ptr,ptr,pp]);codec_free=bind(co,'avcodec_free_context',None,[pp])
    send=bind(co,'avcodec_send_packet',i,[ptr,ptr]);receive=bind(co,'avcodec_receive_frame',i,[ptr,ptr])
    frame_alloc=bind(u,'av_frame_alloc',ptr,[]);frame_unref=bind(u,'av_frame_unref',None,[ptr]);frame_free=bind(u,'av_frame_free',None,[pp])
    bind(u,'av_log_set_level',None,[i])(24)
    version=bind(u,'av_version_info',c.c_char_p,[])().decode()
    findopt=bind(u,'av_opt_find',ptr,[ptr,c.c_char_p,c.c_char_p,i,i])
    if bind(f,'avformat_version',c.c_uint,[])()>>16 !=62:raise RuntimeError('헤더 ABI62와 실제 라이브러리가 다릅니다.')
    fmt=ptr(alloc());options=ptr();codec=ptr();pkt=ptr();frame=ptr();dec=ptr()
    def check(code):
        if code<0:raise RuntimeError('FFmpeg 오류 '+str(code))
    config={'rtsp_transport':'tcp','timeout':'5000000','allowed_media_types':'video','fflags':'nobuffer+discardcorrupt','flags':'low_delay','max_delay':'200000','analyzeduration':'1000000','probesize':'2000000'}
    if args.option:config[args.option]=str(args.buffer)
    for k,v in config.items():check(setdict(c.byref(options),k.encode(),v.encode(),0))
    try:
        check(op(c.byref(fmt),args.url.encode(),None,c.byref(options)))
        leftover={};prev=None
        while True:
            entry=getdict(options,b'',prev,2)
            if not entry:break
            leftover[entry.contents.key.decode()]=entry.contents.value.decode();prev=c.cast(entry,ptr)
        check(info(fmt,None));index=best(fmt,0,-1,-1,c.byref(dec),0);check(index)
        if args.mode=='decode':
            codec=ptr(ctxalloc(dec));check(params(codec,c.cast(fmt,c.POINTER(Format)).contents.streams[index].contents.codecpar))
            # C1은B프레임이 있으므로 플러그인처럼 디코더 LOW_DELAY를 강제로 켜지 않는다.
            opts=ptr();setdict(c.byref(opts),b'threads',b'2',0);setdict(c.byref(opts),b'thread_type',b'frame+slice',0)
            check(codecopen(codec,dec,c.byref(opts)));freedict(c.byref(opts));frame=ptr(frame_alloc())
        pkt=ptr(packet_alloc());began=time.monotonic();next_sample=began;packets=frames=errors=0;wait=waitmax=0
        supported={name:bool(findopt(fmt,name.encode(),None,0,1)) for name in ('recv_buffer_size','buffer_size')}
        (args.out.with_suffix('.meta.json')).write_text(json.dumps(dict(pid=os.getpid(),mode=args.mode,version=version,options=config,unconsumed_options=leftover,rtsp_option_visible=supported,decoder_options={'threads':2,'thread_type':'frame+slice','low_delay':False} if args.mode=='decode' else None,scope='C1 B2 전용. 복사는 비디오 패킷을 수신 후 해제, 디코딩은 SW2스레드·표시/색 변환 없음',started_epoch=time.time()),ensure_ascii=False,indent=2),encoding='utf-8')
        with args.out.open('w',encoding='utf-8',buffering=1) as out:
            while time.monotonic()-began<args.seconds:
                before=time.monotonic();ret=read(fmt,pkt);end=time.monotonic();wait+=end-before;waitmax=max(waitmax,end-before)
                if ret<0:
                    errors+=1
                    out.write(json.dumps(dict(t=end-began,packets=packets,frames=frames,errors=errors,read_error=ret,read_wait_seconds=wait,read_wait_max_seconds=waitmax))+'\n');break
                if c.cast(pkt,c.POINTER(Packet)).contents.stream_index==index:
                    packets+=1
                    if args.mode=='decode':
                        ret=send(codec,pkt)
                        if ret<0:errors+=1
                        while True:
                            ret=receive(codec,frame)
                            if ret in (-11,-541478725):break
                            if ret<0:errors+=1;break
                            frames+=1;frame_unref(frame)
                unref(pkt)
                if end>=next_sample:
                    out.write(json.dumps(dict(t=end-began,packets=packets,frames=frames,errors=errors,read_wait_seconds=wait,read_wait_max_seconds=waitmax))+'\n');next_sample=end+1
        return 0 if errors==0 else 1
    finally:
        if pkt:packet_free(c.byref(pkt))
        if frame:frame_free(c.byref(frame))
        if codec:codec_free(c.byref(codec))
        if fmt:close(c.byref(fmt))
        freedict(c.byref(options))
if __name__=='__main__':raise SystemExit(main())
