"""CCTV-01K 재실행의 중간 결과와 최종 보고를 원시 자료에서 작성한다."""
from pathlib import Path
import argparse,datetime as dt,json,subprocess

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('report',type=Path);ap.add_argument('--final',action='store_true');args=ap.parse_args()
    r=args.report.resolve();root=r.parent;config=read(r/'configuration.json');baseline=read(r/'baseline.json')
    prior=read(root/'first-run-preserved/result.json');controller=read(r/'controller.json') if (r/'controller.json').exists() else {}
    cases=[]
    for item in config['plan']:
        case=r/'measurements'/item['tag'];p=case/'analysis.json'
        value=read(p) if p.exists() else {'status':'NOT_RUN','metrics':None,'failures':[],'unverified':[]}
        cases.append(dict(item,**value))
    counts={s:sum(x['status']==s for x in cases) for s in ['PASS','FAIL','ENV_BLOCKED','NOT_RUN']}
    planned={x['tag'] for x in config['plan']};auxiliary=[]
    for p in sorted((r/'measurements').glob('*/execution.json')):
        if p.parent.name not in planned:auxiliary.append(dict(read(p),tag=p.parent.name))
    manifest_path=Path(r'C:\Users\ho270\AppData\Local\Temp\cctv\dl\rpm-tc\download-manifest.json')
    manifest=read(manifest_path)
    status='RUNNING'
    if args.final:status='FAIL' if counts['FAIL'] else 'ENV_BLOCKED' if counts['ENV_BLOCKED'] or counts['NOT_RUN'] else 'PASS'
    head=subprocess.check_output(['git','rev-parse','HEAD']).decode().strip()
    value=dict(task_id='CCTV-01K',execution='공식 tc RPM 승인 후 재실행',status=status,updated_at=dt.datetime.now().astimezone().isoformat(),
        started_at=baseline['started_at'],base_commit=baseline['head'],head_commit=head,source_cpp=config['source_cpp'],controller=controller,
        first_run=dict(status=prior['status'],result='first-run-preserved/result.json',report='first-run-preserved/RESULT.md',preserved=True),
        download_manifest=manifest,rpm_cache=str(manifest_path.parent),cases=cases,counts=counts,auxiliary_and_invalid_attempts=auxiliary,primary_linux_cases=28,supplemental_windows_tcp_cases=14,
        windows_udp_status='NOT_RUN',windows_udp_note='현재 TCP 바이트 중계는 UDP 미디어 경로를 중계하지 못함. Linux UDP 시험과 구분.',
        known_limits=['Linux NullRHI는 실제 디코딩 범위. 화면·렌더 정상 여부·단절 중 마지막 화면 유지는 미검증.',
            '현재 frame age는 디코드 반환~제출이다. 네트워크 포함 지연은 미측정이며 추가50/150ms를 빼서 판정하지 않음.',
            'UDP DecodeErrors는 모든 손상 프레임 수가 아님. 정확한 손상 프레임 계수는 미측정.',
            '자원은10~20초 정상 구간의 Private/스레드 최댓값과 해제20초 뒤의 최댓값을 엄격 비교. 무장애 대조도 초기화·캐시 증가를 포함하므로 원시 기준 미달과 누적 결함 해석을 구분.',
            'Windows N1-a1은 종료 직후 자기PID를 다른 작업으로 오인한 제어 경합으로 무효 보존. PID/생성시각/경로 식별 보완 후 a2를 계획.',
            '직접 UDP 경로는 무장애부터0프레임이어서 준비 차단으로 중단·미채택. 새 소유 컨테이너의 loopback30000~30127 UDP 게시와 RTSP 포트 중계로25초16Playing을 확인하고 본 시험을 재개. RTP/RTCP는 실제 UDP 데이터그램이며 TCP 영상 터널을 사용하지 않음.'],
        constraints=dict(plugin_code_modified=False,defaults_modified=False,ffmpeg_modified=False,images_downloaded=False,images_modified=False,host_network_changed=False,wsl_network_changed=False,privileged_used=False,host_network_mode_used=False,push=False,pr=False,merge=False,saved_committed=False),
        evidence=['rpm-retest/configuration.json','rpm-retest/baseline.json','rpm-retest/netem-result.json','rpm-retest/control-init.log','rpm-retest/rpm-signatures-final.log','rpm-retest/rpm-install.log','rpm-retest/ownership-validation.json'])
    for name in ['cleanup-audit.json','scope-verification.json','static-validation.json']:
        if (r/name).exists():value[name[:-5].replace('-','_')]=read(r/name)
    write(root/'result.json',value)
    state='최종' if args.final else '진행 중'
    md=f"# CCTV-01K RPM 승인 후 재실행 ({state})\n\n판정: **{status}** / 갱신: {value['updated_at']}\n\n"
    md+='1차 ENV_BLOCKED 원문은 [RESULT](first-run-preserved/RESULT.md)와 [JSON](first-run-preserved/result.json)에 보존했다. 이번 결과로 이전 자료를 덮어쓰지 않았다.\n\n'
    md+=f"기준 C++ `{config['source_cpp']}`, 현재 커밋 `{head}`. 플러그인 코드·기본값·FFmpeg 변경 없이 J 최종 패키지를 사용한다.\n\n"
    md+=f"공식 Rocky8 RPM9개 총 **{manifest['rpm_bytes']:,}바이트**, 필요한8개만 새 컨테이너에 설치했다. libnl3는 기존 설치를 먼저 확인하지 못해 중복 다운로드했지만 설치하지 않았다. 모든 다운로드는 Z까지 보존한다. 공식 키 지문·RPM 서명/다이제스트를 검증했다. [공식 키 정보](https://rockylinux.org/resources/gpg-key-info).\n\n"
    md+='기존 이미지 불변, RPM 읽기 전용 마운트, 추가 권한 NET_ADMIN만 사용했다. netem·IFB·실제 수신 제어 경로 준비는 통과했다. Windows TCP는 컨테이너 중계의 원본8554포트 수신에만 장애를 적용한다.\n\n'
    md+=f"Linux28개 및 Windows TCP14개 계획: PASS {counts['PASS']}, FAIL {counts['FAIL']}, ENV_BLOCKED {counts['ENV_BLOCKED']}, NOT_RUN {counts['NOT_RUN']}. 각 실행은185초, 정상20초 뒤132초 장애 구간에서 초기10초를 제외한120초 이상을 분석하고, 해제 뒤30초 이상을 관측한다.\n\n"
    md+='| 플랫폼·조건 | 판정 | 디코딩 평균/최저 fps | 재연결 | 복구 상한 | 해제 후 Private/스레드 증감 |\n|---|---|---|---|---|---|\n'
    for x in cases:
        m=x.get('metrics') or {};res=m.get('resources') or {}
        fps=f"{m['decode_fps']:.3f}/{m['min_channel_decode_fps']:.3f}" if 'decode_fps' in m else '미측정'
        mem=f"{res['private_delta_kb']/1024:+.2f}MiB/{res['threads_delta']:+d}" if res else '미측정'
        md+=f"| {x['platform']} {x['codec']}/{x['transport']} {x['condition']} | {x['status']} | {fps} | {m.get('reconnects','미측정')} | {m.get('recovery_upper_seconds','미측정')} | {mem} |\n"
    md+='\n## 해석과 제한\n\n'+'\n'.join('- '+x for x in value['known_limits'])+'\n\n'
    md+='무장애 대조와 UE를 제외한 FFmpeg1/16클라이언트 대조는 rpm-retest/source-control* 및 linux-h264-tcp-N0-control-a1에 보존한다. 송출 서버의 reader is too slow 프레임 폐기, nstat/ss 재전송·재정렬, qdisc backlog/drop도 원문으로 남긴다. 이 증거 없이 모든 저하를 플러그인 결함으로 확정하지 않는다. 기준을 넘지 못한 데이터는 PASS로 바꾸지 않는다.\n\n'
    md+='각 케이스의 execution.json, analysis.json, network-events.jsonl, qdisc/nstat/ss, CPU·Private·스레드, 채널CSV 및 가능한 PNG가 rpm-retest/measurements/에 있다. 게이트마다 [progress.md](progress.md)와 checkpoint를 갱신한다.\n'
    if args.final:md+='\n최종 정리·범위 감사는 result.json의 cleanup_audit/scope_verification을 참조한다. 추가 기본값 변경은 승인받지 않았으며 적용하지 않았다.\n'
    (root/'RESULT.md').write_text(md,encoding='utf-8')
    print(json.dumps(dict(status=status,counts=counts,active=controller.get('tag')),ensure_ascii=False))
if __name__=='__main__':main()
