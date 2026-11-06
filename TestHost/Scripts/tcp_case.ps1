param([ValidateSet('plugin','plugin-auto','copy','decode')][string]$Mode,[ValidateSet(1,4,8,16)][int]$Count,[string]$Tag,
    [ValidateSet('N0','N1','N2','N3','N4','N7')][string]$Condition='N1',[ValidateSet('','buffer_size','recv_buffer_size')][string]$Option='', [ValidateRange(0,16777216)][int]$BufferBytes=0)
$ErrorActionPreference='Stop'
$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
$env:PYTHONIOENCODING='utf-8'
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $repo
. "$PSScriptRoot\DTCctvTestCommon.ps1"
$report="$repo\Saved\Reports\CCTV-01T"
$case=Join-Path $report "measurements\$Tag"
$waitStart=[datetime]::UtcNow
while($true)
{
    try{Assert-DTCctvIdle;break}
    catch
    {
        & python "$report\gate.py" "$Tag-wait" NOT_RUN $_.Exception.Message
        if(([datetime]::UtcNow-$waitStart).TotalMinutes -ge 30){& python "$report\gate.py" $Tag BLOCKED_BY_OTHER_WORK '다른 작업이30분 동안 계속됨';exit 3}
        $until=[datetime]::UtcNow.AddMinutes(5);while([datetime]::UtcNow -lt $until){Start-Sleep -Seconds 5}
    }
}
if(Test-Path -LiteralPath $case){throw '이전 측정 자료를 덮어쓰지 않습니다.'}
New-Item -ItemType Directory -Path "$case\saved" -Force|Out-Null
$lock=Enter-DTCctvTestLock
$owned=[Collections.Generic.List[object]]::new();$netId=$null;$playerId=$null
$cfg=Get-Content -Raw -Encoding UTF8 "$report\configuration.json"|ConvertFrom-Json
$result=[ordered]@{task_id='CCTV-01T';tag=$Tag;status='FAIL';mode=$Mode;channels=$Count;condition=$Condition;option=$Option;buffer_bytes=$BufferBytes;started_at=(Get-Date -Format o);seconds=160;warmup_seconds=10;note='';sysctl_changed=$false;no_download=$true}
function NetControl([string]$Action,[string]$Scenario='')
{
    $cmd=@('exec','-e','DTCCTV_NETEM_OWNED=1',$netId,'/bin/sh','/control.sh',$Action)
    if($Scenario){$cmd+=@($Scenario)}
    & docker @cmd *> "$case\control-$Action-$Scenario.log"
    if($LASTEXITCODE -ne 0){throw "컨테이너 한정 netem 실패: $Action $Scenario"}
}
try
{
    $hashes=@{};foreach($name in @('tcp_case.ps1','tcp_runtime.py','tcp_receiver.py','netem_control.sh','netem_runtime.js')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash};Write-DTCctvJson "$case\script-hashes.json" $hashes
    $sourceHash=(Get-FileHash -LiteralPath $cfg.sources.h264.file -Algorithm SHA256).Hash.ToLowerInvariant()
    if($sourceHash -ne $cfg.sources.h264.sha256){throw 'C1 입력 파일 해시 불일치'}
    $result['source_sha256']=$sourceHash
    $cpp=(& git log -1 --format=%H -- Source TestHost/Source).Trim()
    if(-not $cpp.StartsWith($cfg.source_cpp)){throw '패키징 뒤 C++가 바뀌었습니다.'}
    $result['source_cpp']=$cpp
    $cp=Get-Content -Raw -Encoding UTF8 "$report\checkpoint.json"|ConvertFrom-Json
    $cp.active_measurement=@{tag=$Tag;started_at=$result.started_at};Write-DTCctvJson "$report\checkpoint.json" $cp
    & python -c "import sys;sys.path.insert(0,'TestHost/Scripts');from network_fault_test import validate_rpm_cache;from pathlib import Path;validate_rpm_cache(Path(sys.argv[1]))" $cfg.rpm_dir
    if($LASTEXITCODE -ne 0){throw '승인 RPM 해시 검증 실패'}
    $config="$case\mediamtx-test.yml"
    [IO.File]::WriteAllText($config,"logLevel: info`nlogDestinations: [stdout]`nrtsp: yes`nrtspTransports: [tcp, udp]`nrtspAddress: :8554`nrtmp: no`nhls: no`nwebrtc: no`nsrt: no`npaths:`n  all_others:`n",[Text.UTF8Encoding]::new($false))
    $server=Start-DTCctvOwnedProcess -File (Join-Path $cfg.tools.mediamtx 'mediamtx.exe') -Arguments ('"'+$config+'"') -WorkingDirectory $case -LogPrefix "$case\server" -Owned $owned
    Start-Sleep -Seconds 1
    $pubArgs='-hide_banner -loglevel warning -stream_loop -1 -re -i "'+$cfg.sources.h264.file+'" -map 0:v:0 -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam'
    $pub=Start-DTCctvOwnedProcess -File $cfg.tools.ffmpeg -Arguments $pubArgs -WorkingDirectory $case -LogPrefix "$case\publisher" -Owned $owned
    $deadline=[datetime]::UtcNow.AddSeconds(25)
    do
    {
        if($pub.Process.HasExited -or $server.Process.HasExited){throw '자체 송출 프로세스 종료'}
        $ready=Select-String -LiteralPath "$case\server.stdout.log" -SimpleMatch "is publishing to path 'cam'" -Quiet -ErrorAction SilentlyContinue
        if($ready){break};Start-Sleep -Milliseconds 100
    }while([datetime]::UtcNow -lt $deadline)
    if(-not $ready){throw '송출 준비 로그 미확인'}
    $create=@('create','--pull=never','--user','0:0','--cap-add','NET_ADMIN','--security-opt','no-new-privileges','--label','dtcctv.task=CCTV-01T',
        '--mount',"type=bind,source=$($cfg.rpm_dir),target=/rpms,readonly",'--mount',"type=bind,source=$case,target=/reports",
        '--mount',"type=bind,source=$PSScriptRoot,target=/check,readonly",'--mount',"type=bind,source=$PSScriptRoot\netem_runtime.js,target=/runtime.js,readonly",
        '--mount',"type=bind,source=$PSScriptRoot\netem_control.sh,target=/control.sh,readonly",'--entrypoint','/bin/sh',$cfg.image,
        '-c','DTCCTV_NETEM_OWNED=1 /bin/sh /check/netem_rpm_setup.sh && touch /reports/network.ready && exec sleep 600')
    $netId=[string](& docker @create 2> "$case\network-create.log")
    if($LASTEXITCODE -ne 0 -or $netId -notmatch '^[0-9a-f]{64}$'){throw '소유 네트워크 컨테이너 생성 실패'}
    Write-DTCctvJson "$case\network-owned-container.json" @{id=$netId;image=$cfg.image;created_at=(Get-Date -Format o)}
    & docker start $netId *> "$case\network-start.log"
    $deadline=[datetime]::UtcNow.AddSeconds(35)
    while(-not(Test-Path -LiteralPath "$case\network.ready")){if([datetime]::UtcNow -gt $deadline){throw '컨테이너 내 RPM 설치 준비 제한 초과'};Start-Sleep -Milliseconds 100}
    & docker logs $netId *> "$case\rpm-install.log"
    NetControl init
    if($Condition -ne 'N0'){NetControl apply $Condition}
    & docker exec $netId /bin/sh -c 'cat /proc/sys/net/core/rmem_max; cat /proc/sys/net/ipv4/tcp_rmem' *> "$case\socket-sysctl.log"
    New-Item -ItemType Directory -Force -Path (Join-Path $cfg.packages 'Linux\DTCctvTestHost\Saved')|Out-Null
    $create=@('create','--pull=never','--user','1000:1000','--network',"container:$netId",'--read-only','--tmpfs','/tmp:rw,exec,mode=1777','--label','dtcctv.task=CCTV-01T',
        '--mount',"type=bind,source=$($cfg.packages)\Linux,target=/package,readonly",'--mount',"type=bind,source=$case,target=/reports",
        '--mount',"type=bind,source=$case\saved,target=/package/DTCctvTestHost/Saved",'--mount',"type=bind,source=$PSScriptRoot,target=/check,readonly",
        '--env','LD_LIBRARY_PATH=/package/DTCctvTestHost/Binaries/Linux','--entrypoint','/usr/libexec/platform-python',$cfg.image,
        '/check/tcp_runtime.py',$Mode,'--count',[string]$Count,'--seconds','160')
    if($Mode -eq 'plugin-auto')
    {
        $imageIndex=[Array]::IndexOf($create,[string]$cfg.image)
        $create=@($create[0..($imageIndex-1)])+@('--gpus','all','--env','NVIDIA_DRIVER_CAPABILITIES=all')+@($create[$imageIndex..($create.Length-1)])
    }
    if($Option){$create+=@('--option',$Option,'--buffer',[string]$BufferBytes)}
    $playerId=[string](& docker @create 2> "$case\player-create.log")
    if($LASTEXITCODE -ne 0 -or $playerId -notmatch '^[0-9a-f]{64}$'){throw '자체 수신기 컨테이너 생성 실패'}
    Write-DTCctvJson "$case\player-owned-container.json" @{id=$playerId;network_owner=$netId;created_at=(Get-Date -Format o)}
    $runner=Start-DTCctvOwnedProcess -File (Get-Command docker).Source -Arguments "start -a $playerId" -WorkingDirectory $case -LogPrefix "$case\runner" -Owned $owned
    $began=[datetime]::UtcNow;$nextSocket=$began;$seq=0
    while(-not $runner.Process.WaitForExit(500))
    {
        Assert-DTCctvMeasurementIdle -Owned $owned
        if($pub.Process.HasExited -or $server.Process.HasExited){throw '측정 중 송출 종료'}
        if([datetime]::UtcNow -ge $nextSocket)
        {
            & docker exec $netId ss -tinm *> (Join-Path $case ('ss-{0:d3}.log' -f $seq))
            $seq++;$nextSocket=[datetime]::UtcNow.AddSeconds(5)
        }
        if(([datetime]::UtcNow-$began).TotalSeconds -gt 220){throw '수신기 측정 종료 제한 초과'}
    }
    Assert-DTCctvMeasurementIdle -Owned $owned -Force
    $result['native_exit_code']=$runner.Process.ExitCode
    if($runner.Process.ExitCode -ne 0)
    {
        if(-not(Test-Path -LiteralPath "$case\runtime-result.json")){throw '수신기 실행 도구 비정상 종료. 원시 로그 확인 필요'}
        $result.note='대조 수신기 오류 종료 포함. 종료 코드와 프레임 수를 보존하고 분석에서 FAIL로 판정'
    }
    $result.status='MEASURED'
}
catch
{
    $result.note=$_.Exception.Message
    if($_.Exception -is [OperationCanceledException]){$result.status='INVALID'}
}
finally
{
    if($netId -match '^[0-9a-f]{64}$')
    {
        & docker exec $netId /sbin/tc -s qdisc show dev dtcctvifb *> "$case\qdisc-final.log"
        & docker exec $netId nstat -asz *> "$case\nstat-final.log"
        & docker inspect $netId *> "$case\network-inspect.json"
    }
    foreach($id in @($playerId,$netId))
    {
        if($id -match '^[0-9a-f]{64}$'){& docker rm -f $id *> "$case\cleanup-$id.log";if($LASTEXITCODE -ne 0){$result.status='FAIL';$result.note+=' 자체 컨테이너 정리 실패'}}
    }
    try{Close-DTCctvOwnedProcesses $owned}catch{$result.status='FAIL';$result.note+=$_.Exception.Message}
    $result['finished_at']=Get-Date -Format o
    Write-DTCctvJson "$case\execution.json" $result
    $gateNote=if($result.note){$result.note}else{'원시160초 측정 완료. 판정은 analysis.json에 별도로 기록'}
    & python "$report\gate.py" $Tag $result.status $gateNote
    $lock.ReleaseMutex();$lock.Dispose()
}
$result|ConvertTo-Json -Depth 6
if($result.status -ne 'MEASURED'){exit 1}
