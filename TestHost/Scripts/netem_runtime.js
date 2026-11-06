// 컨테이너 안에서만 실행하는 RPM 검증·TCP 중계·자원 관측 보조 도구.
const fs = require('fs');
const crypto = require('crypto');
const cp = require('child_process');
const net = require('net');
const path = require('path');

function fingerprint(text) {
  const raw = Buffer.from(text.split(/\r?\n/).filter(x => x && !/^(---|Version:|Comment:|=)/.test(x)).join(''), 'base64');
  const header = raw[0];
  if (!(header & 128) || (header & 64) || ((header >> 2) & 15) !== 6) throw Error('공개키 패킷 형식 불일치');
  const lengths = [1, 2, 4]; const count = lengths[header & 3];
  if (!count) throw Error('공개키 길이 형식 미지원');
  const size = raw.readUIntBE(1, count); const body = raw.subarray(1 + count, 1 + count + size);
  if (body[0] !== 4 || body.length !== size || size > 65535) throw Error('공개키 본문 불일치');
  const prefix = Buffer.alloc(3); prefix[0] = 153; prefix.writeUInt16BE(size, 1);
  return crypto.createHash('sha1').update(prefix).update(body).digest('hex').toUpperCase();
}
function installRpms(folder) {
  const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'download-manifest.json'), 'utf8'));
  const key = manifest.verification_key;
  if (key.url !== 'https://download.rockylinux.org/pub/rocky/RPM-GPG-KEY-Rocky-8') throw Error('공식 키 URL 불일치');
  if (fingerprint(fs.readFileSync(path.join(folder, key.file), 'utf8')) !== '7051C470A929F454CEBE37B715AF5DAC6D745A60') throw Error('공식 키 지문 불일치');
  let bytes = 0; const selected = [];
  for (const row of manifest.packages) {
    if (!/^[a-zA-Z0-9+_.-]+\.rpm$/.test(row.file) || !row.url.startsWith('https://download.rockylinux.org/pub/rocky/8/BaseOS/x86_64/os/Packages/')) throw Error('승인 RPM 경로 불일치');
    const data = fs.readFileSync(path.join(folder, row.file)); bytes += data.length;
    if (data.length !== row.bytes || crypto.createHash('sha256').update(data).digest('hex') !== row.sha256) throw Error('RPM 해시/크기 불일치');
    if (row.install_required !== false) selected.push(path.join(folder, row.file));
  }
  if (bytes > 50000000 || !selected.length) throw Error('승인 용량/패키지 조건 불충족');
  const run = args => {
    const result = cp.spawnSync('rpm', args, {encoding:'utf8'});
    process.stdout.write(result.stdout || ''); process.stderr.write(result.stderr || '');
    if (result.status !== 0 || /NOKEY|NOT OK|BAD signature/.test((result.stdout || '') + (result.stderr || ''))) throw Error('RPM 검증/설치 실패');
  };
  run(['--import', path.join(folder, key.file)]);
  run(['-Kv', ...selected]); run(['-i', '--test', ...selected]); run(['-ivh', ...selected]);
  console.log(JSON.stringify({rpm_bytes:bytes,installed:selected.length,signature_verified:true}));
}
function stat(pid) {
  const raw = fs.readFileSync(`/proc/${pid}/stat`, 'utf8');
  const fields = raw.slice(raw.lastIndexOf(')') + 2).split(' ');
  const status = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
  let privateKb = null;
  try {
    const smaps = fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8');
    privateKb = [...smaps.matchAll(/^Private_(?:Clean|Dirty):\s+(\d+)/gm)].reduce((a,x) => a + Number(x[1]),0);
  } catch {}
  return {start_ticks:fields[19],threads:Number(fields[17]),cpu_ticks:Number(fields[11])+Number(fields[12]),
    rss_kb:Number((status.match(/^VmRSS:\s+(\d+)/m)||[])[1]||0),private_kb:privateKb};
}
function player(out, command) {
  const child = cp.spawn(command[0], command.slice(1), {stdio:'inherit'});
  const initial = stat(child.pid);
  fs.writeFileSync(out + '.process.json', JSON.stringify({pid:child.pid,start_ticks:initial.start_ticks,path:command[0]}));
  const stream = fs.createWriteStream(out, {flags:'wx'});
  const began=Date.now();const dumps=new Set();
  const tick = () => { try {
    const value=stat(child.pid); if(value.start_ticks !== initial.start_ticks) throw Error('PID 생성 식별자 불일치');
    stream.write(JSON.stringify({utc:new Date().toISOString(),...value})+'\n');
    for(const [name,seconds] of [['before',15],['after',175]]){
      if(!dumps.has(name) && Date.now()-began>=seconds*1000){
        fs.writeFileSync(out+'.'+name+'.smaps',fs.readFileSync(`/proc/${child.pid}/smaps`));dumps.add(name);
      }
    }
  } catch(e) { if(e.code !== 'ENOENT') console.error(e.message); } };
  tick(); const timer=setInterval(tick,1000);
  const stop = () => { try { if(stat(child.pid).start_ticks === initial.start_ticks) child.kill('SIGTERM'); } catch {} };
  process.on('SIGTERM', stop); process.on('SIGINT', stop);
  child.on('error', e => { console.error(e.message); clearInterval(timer); stream.end(); process.exitCode=1; });
  child.on('exit',(code,signal) => { clearInterval(timer); stream.end(); fs.writeFileSync(out+'.exit.json',JSON.stringify({code,signal,utc:new Date().toISOString()})); process.exitCode=code === null ? 1 : code; });
}
function relay(port, upstream, upstreamPort) {
  fs.writeFileSync('/tmp/dtcctv-relay-owner.json',JSON.stringify({pid:process.pid,start_ticks:stat(process.pid).start_ticks,command:process.argv}));
  const sockets=new Set();
  const server=net.createServer(client => {
    const remote=net.connect(upstreamPort,upstream); sockets.add(client);sockets.add(remote);
    client.pipe(remote);remote.pipe(client);
    const close=()=>{client.destroy();remote.destroy();sockets.delete(client);sockets.delete(remote);};
    client.on('error',close);remote.on('error',close);client.on('close',close);remote.on('close',close);
  });
  server.listen(port,'0.0.0.0',()=>console.log('TCP_RELAY_READY'));
  const stop=()=>{for(const s of sockets)s.destroy();server.close(()=>{try{fs.unlinkSync('/tmp/dtcctv-relay-owner.json');}catch{}process.exit(0);});};
  process.on('SIGTERM',stop);process.on('SIGINT',stop);
}
try {
  const [mode,...args]=process.argv.slice(2);
  if(mode==='rpm')installRpms(args[0]);
  else if(mode==='player')player(args[0],args.slice(1));
  else if(mode==='relay')relay(Number(args[0]),args[1],Number(args[2]));
  else if(mode==='stop-relay'){
    if(fs.existsSync('/tmp/dtcctv-relay-owner.json')){
      const owner=JSON.parse(fs.readFileSync('/tmp/dtcctv-relay-owner.json','utf8'));
      const command=fs.readFileSync(`/proc/${owner.pid}/cmdline`,'utf8').split('\0').filter(Boolean);
      if(stat(owner.pid).start_ticks !== owner.start_ticks || JSON.stringify(command.slice(1))!==JSON.stringify(owner.command.slice(1)) || fs.realpathSync(`/proc/${owner.pid}/exe`)!==fs.realpathSync(owner.command[0]))throw Error('중계 PID 소유권 불일치');
      process.kill(owner.pid,'SIGTERM');
    }
  }
  else throw Error('지원하지 않는 보조 작업');
} catch(e) { console.error(e.stack);process.exitCode=1; }
