param(
    [Parameter(Mandatory=$true)][string]$ReportRoot,
    [Parameter(Mandatory=$true)][string]$Tag,
    [ValidateSet('linux','windows')][string]$Platform='linux',
    [ValidateSet('h264','hevc')][string]$Codec='h264',
    [ValidateSet('tcp','udp')][string]$Transport='tcp',
    [ValidateSet('N0','N1','N2','N3','N4','N5','N6','N7')][string]$Condition='N1',
    [switch]$PrepareOnly
)
$ErrorActionPreference='Stop'
if($PrepareOnly -and $Condition -ne 'N0'){throw '짧은 준비 점검은 무장애 N0에만 사용할 수 있습니다.'}
$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $repo
. "$PSScriptRoot\DTCctvTestCommon.ps1"
$case=Join-Path $ReportRoot "measurements\$Tag"
if(Test-Path -LiteralPath $case){throw '기존 측정 자료를 덮어쓰지 않습니다.'}
New-Item -ItemType Directory -Force -Path "$case\saved" | Out-Null
$state=Get-Content -Raw -Encoding UTF8 "$ReportRoot\configuration.json" | ConvertFrom-Json
if($Platform -eq 'linux' -and $Transport -eq 'udp')
{
    Assert-DTCctvIdle
    & python "$PSScriptRoot\prepare_netem_owner.py" $ReportRoot *> "$case\owner-prepare.log"
    if($LASTEXITCODE -ne 0){throw '독립 UDP 네트워크 도구 준비 실패'}
}
$netOwner=Get-Content -Raw -Encoding UTF8 "$ReportRoot\network-owner.json" | ConvertFrom-Json
$netId=$netOwner.id
if($netId -notmatch '^[0-9a-f]{64}$'){throw '소유 네트워크 컨테이너 ID 오류'}
$inspected=@(& docker inspect $netId | ConvertFrom-Json)
if($LASTEXITCODE -ne 0 -or $inspected[0].Config.Labels.'dtcctv.task' -ne 'CCTV-01K-rpm-retest'){throw '소유 네트워크 컨테이너 검증 실패'}
Assert-DTCctvIdle
$lock=Enter-DTCctvTestLock
$owned=[Collections.Generic.List[object]]::new()
$container=$null;$initialized=$false;$reader=$null;$n6=$null
$seconds=if($PrepareOnly){25}else{185}
$result=[ordered]@{status='FAIL';tag=$Tag;platform=$Platform;codec=$Codec;transport=$Transport;condition=$Condition;started_at=(Get-Date -Format o);source_cpp=$state.source_cpp;game_seconds=$seconds;impairment_seconds_requested=132;evaluation_seconds_requested=120;channels=16;original_fps=30;prepare_only=[bool]$PrepareOnly;note=''}
if($PrepareOnly){$result.impairment_seconds_requested=0;$result.evaluation_seconds_requested=0}
$result['script_hashes']=@{}
foreach($name in @('DTCctvTestCommon.ps1','netem_runtime.js','netem_control.sh','network_fault_case.ps1','prepare_netem_owner.py')){$result.script_hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash}
$checkpointPath=Join-Path (Split-Path $ReportRoot) 'checkpoint.json'
$checkpoint=Get-Content -Raw -Encoding UTF8 $checkpointPath | ConvertFrom-Json
$checkpoint|Add-Member -MemberType NoteProperty -Name active_measurement -Value @{tag=$Tag;started_at=$result.started_at;platform=$Platform;codec=$Codec;transport=$Transport;condition=$Condition} -Force
Write-DTCctvJson $checkpointPath $checkpoint
function Get-Clock
{
    $csv=$null
    if($Platform -eq 'windows'){$csv="$case\samples.csv"}
    else
    {
        $files=@(Get-ChildItem -LiteralPath "$case\saved" -Recurse -Filter channel-01.csv -File | Select-Object -First 1)
        if($files.Count -eq 0){return $null}
        $csv=$files[0].FullName
    }
    if(-not $csv -or -not(Test-Path -LiteralPath $csv)){return $null}
    $line=Get-Content -LiteralPath $csv -Tail 1
    if($line -notmatch '^([0-9]+\.[0-9]+),'){return $null}
    return [pscustomobject]@{t=[double]$matches[1];csv=$csv;file_utc=(Get-Item -LiteralPath $csv).LastWriteTimeUtc.ToString('o')}
}
function Save-Event([string]$Name,$Clock)
{
    [pscustomobject]@{event=$Name;t=$(if($Clock){$Clock.t}else{$null});utc=[datetime]::UtcNow.ToString('o')} | ConvertTo-Json -Compress | Add-Content -Encoding UTF8 "$case\network-events.jsonl"
}
function Get-Baseline($Clock)
{
    $sets=@()
    if($Platform -eq 'linux')
    {
        $files=@(Get-ChildItem -LiteralPath "$case\saved" -Recurse -Filter 'channel-*.csv' -File)
        foreach($f in $files){$sets+=,@(Import-Csv -LiteralPath $f.FullName)}
    }
    else
    {
        $groups=@(Import-Csv -LiteralPath "$case\samples.channels.csv" | Group-Object channel)
        foreach($g in $groups){$sets+=,@($g.Group)}
    }
    $checks=@()
    foreach($seq in $sets)
    {
        $end=$seq[-1]
        $begin=@($seq | Where-Object {[double]$_.t -ge [double]$end.t-4})[0]
        $seconds=[double]$end.t-[double]$begin.t
        $fps=if($seconds -gt 0){([long]$end.decoded-[long]$begin.decoded)/$seconds}else{0}
        $checks+=@{playing=[int]$end.state -eq 2;decode_fps=$fps;t=[double]$end.t}
    }
    return $checks
}
function Control([string]$Action,[string]$Scenario='', [long]$Aggregate=0)
{
    $args=@('exec','-e','DTCCTV_NETEM_OWNED=1','-e','DTCCTV_INTERFACE=eth0')
    if($Platform -eq 'windows'){$args+=@('-e','DTCCTV_SOURCE_PORT=8554')}
    $args+=@($netId,'/bin/sh','/control.sh',$Action)
    if($Scenario){$args+=@($Scenario)}
    if($Aggregate -gt 0){$args+=@([string]$Aggregate)}
    & docker @args *> "$case\control-$Action-$Scenario.log"
    if($LASTEXITCODE -ne 0){throw "컨테이너 장애 제어 실패: $Action $Scenario"}
}
try
{
    if($Platform -eq 'windows')
    {
        if($Transport -ne 'tcp'){throw 'Windows 중계는 TCP만 지원합니다.'}
        $desktop=& "$PSScriptRoot\check_input_desktop.ps1" -Out "$case\input-desktop.json" | ConvertFrom-Json
        if($desktop.status -ne 'PASS'){$result.status='ENV_BLOCKED';throw '입력 데스크톱 Default 확인 실패'}
    }
    $oldLevel=[Environment]::GetEnvironmentVariable('MTX_LOGLEVEL','Process')
    $oldDest=[Environment]::GetEnvironmentVariable('MTX_LOGDESTINATIONS','Process')
    try
    {
        [Environment]::SetEnvironmentVariable('MTX_LOGLEVEL','info','Process')
        [Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS','stdout','Process')
        $server=Start-DTCctvOwnedProcess -File (Join-Path $state.tools.mediamtx 'mediamtx.exe') -Arguments 'mediamtx.yml' -WorkingDirectory $state.tools.mediamtx -LogPrefix "$case\source.server" -Owned $owned
    }
    finally
    {
        [Environment]::SetEnvironmentVariable('MTX_LOGLEVEL',$oldLevel,'Process')
        [Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS',$oldDest,'Process')
    }
    Start-Sleep -Seconds 1
    $file=if($Codec -eq 'h264'){$state.sources.h264.file}else{$state.sources.hevc.file}
    $publisherArgs='-hide_banner -loglevel warning -stream_loop -1 -re -i "'+$file+'" -map 0:v:0 -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam'
    $publisher=Start-DTCctvOwnedProcess -File $state.tools.ffmpeg -Arguments $publisherArgs -WorkingDirectory $repo -LogPrefix "$case\source.publisher" -Owned $owned
    $deadline=[datetime]::UtcNow.AddSeconds(25)
    do
    {
        if($publisher.Process.HasExited -or $server.Process.HasExited){throw '직접 시작한 송출 프로세스 종료'}
        $ready=@(Select-String -LiteralPath "$case\source.server.stdout.log","$case\source.server.stderr.log" -SimpleMatch "is publishing to path 'cam'" -ErrorAction SilentlyContinue).Count -gt 0
        if($ready){break};Start-Sleep -Milliseconds 100
    }while([datetime]::UtcNow -lt $deadline)
    if(-not $ready){throw '송출 준비 로그 확인 실패'}
    Control init
    $initialized=$true
    $docker=(Get-Command docker).Source
    if($Platform -eq 'linux')
    {
        $url='rtsp://host.docker.internal:8554/cam'
        if($Transport -eq 'udp')
        {
            $relay=Start-DTCctvOwnedProcess -File $docker -Arguments "exec $netId node /runtime.js udp-relay 18555 host.docker.internal 8554 30000 64" -WorkingDirectory $repo -LogPrefix "$case\udp-relay" -Owned $owned
            $deadline=[datetime]::UtcNow.AddSeconds(10)
            while(-not(Select-String -LiteralPath "$case\udp-relay.stdout.log" -SimpleMatch UDP_RELAY_READY -Quiet -ErrorAction SilentlyContinue)){if([datetime]::UtcNow -gt $deadline){throw 'UDP 중계 준비 실패'};Start-Sleep -Milliseconds 100}
            $url='rtsp://127.0.0.1:18555/cam'
        }
        $channels=@();for($i=0;$i -lt 16;$i++){$channels+=@{url=$url;transport=$Transport}}
        Write-DTCctvJson "$case\input.json" @{schema_version=1;channels=$channels}
        $create=@('create','--pull=never','--user','1000:1000','--network',"container:$netId",'--read-only','--tmpfs','/tmp:rw,exec,mode=1777','--gpus','all','--env','NVIDIA_DRIVER_CAPABILITIES=all','--env','LD_LIBRARY_PATH=/package/DTCctvTestHost/Binaries/Linux',
            '--mount',"type=bind,source=$($state.packages)\Linux,target=/package,readonly",'--mount',"type=bind,source=$case\saved,target=/package/DTCctvTestHost/Saved",'--mount',"type=bind,source=$case,target=/reports",'--mount',"type=bind,source=$PSScriptRoot\netem_runtime.js,target=/runtime.js,readonly",'--label','dtcctv.task=CCTV-01K-rpm-retest','--entrypoint','/usr/local/bin/node',$netOwner.image,'/runtime.js','player','/reports/resources.jsonl','/package/DTCctvTestHost/Binaries/Linux/DTCctvTestHost','-nullrhi','-unattended','-nosplash','-nosound','-CctvCheckList=/reports/input.json',"-CctvCheckSeconds=$seconds",'-ExecCmds=t.MaxFPS 60','-abslog=/reports/game.log')
        $created=& docker @create 2> "$case\create.stderr.log"
        if($LASTEXITCODE -ne 0 -or $created -notmatch '^[0-9a-f]{64}$'){throw 'Linux 자체 플레이어 컨테이너 생성 실패'}
        $container=[string]$created
        Write-DTCctvJson "$case\owned-container.json" @{id=$container;created_at=(Get-Date -Format o);scope='단일 Linux 장애 측정 플레이어';network_owner=$netId}
        $reader=Start-DTCctvOwnedProcess -File $docker -Arguments "start -a $container" -WorkingDirectory $repo -LogPrefix "$case\game-client" -Owned $owned
    }
    else
    {
        $relay=Start-DTCctvOwnedProcess -File $docker -Arguments "exec $netId node /runtime.js relay 18554 host.docker.internal 8554" -WorkingDirectory $repo -LogPrefix "$case\relay" -Owned $owned
        $deadline=[datetime]::UtcNow.AddSeconds(10)
        while(-not(Select-String -LiteralPath "$case\relay.stdout.log" -SimpleMatch TCP_RELAY_READY -Quiet -ErrorAction SilentlyContinue)){if([datetime]::UtcNow -gt $deadline){throw 'TCP 중계 준비 실패'};Start-Sleep -Milliseconds 100}
        $argsLine='-windowed -ForceRes -ResX=1920 -ResY=1080 -unattended -nosplash -nosound -CctvUrl=rtsp://127.0.0.1:18554/cam -CctvCount=16 -CctvSeconds=185 -CctvCsv="'+$case+'\samples.csv" -CctvShot="'+$case+'\final.png" -ExecCmds="r.VSync 0,t.MaxFPS 60" -abslog="'+$case+'\game.log"'
        if($Condition -eq 'N6'){$argsLine+=' -CctvShotEvery=1'}
        $exe=Join-Path $state.packages 'Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe'
        $reader=Start-DTCctvOwnedProcess -File $exe -Arguments $argsLine -WorkingDirectory (Split-Path $exe) -LogPrefix "$case\game" -Owned $owned
    }
    $phaseStart=$null;$cleared=$false;$outageStarted=$false;$wireFirst=$null;$wireAt=$null;$wireLast=$null
    $nextResource=[datetime]::UtcNow;$deadline=[datetime]::UtcNow.AddSeconds(300)
    & docker exec $netId nstat -asz *> "$case\nstat-before.log"
    while(-not $reader.Process.HasExited)
    {
        Assert-DTCctvMeasurementIdle -Owned $owned
        if($publisher.Process.HasExited -or $server.Process.HasExited){$result.status='ENV_BLOCKED';throw '측정 도중 송출 프로세스가 종료돼 시험 경로가 유효하지 않음'}
        $clock=Get-Clock
        if($clock)
        {
            $clock | ConvertTo-Json -Compress | Add-Content -Encoding UTF8 "$case\game-clock.jsonl"
            $t=$clock.t
            if($Platform -eq 'windows' -and [datetime]::UtcNow -ge $nextResource){Save-DTCctvResources -Entry $reader -Csv "$case\resources.csv";$nextResource=[datetime]::UtcNow.AddSeconds(2)}
            if($t -ge 10 -and $null -eq $wireFirst){$wireFirst=[long](& docker exec $netId cat /sys/class/net/dtcctvifb/statistics/rx_bytes);$wireAt=$t}
            if($t -ge 20 -and $null -eq $phaseStart)
            {
                $checks=@(Get-Baseline $clock)
                $result['baseline_channels']=$checks
                if($checks.Count -ne 16 -or @($checks | Where-Object {-not $_.playing -or $_.decode_fps -le 0}).Count -gt 0)
                {
                    $result.status='ENV_BLOCKED'
                    throw '장애 적용 전16채널 Playing/새 프레임 수신 경로가 성립하지 않아 측정 미수행'
                }
                $wireLast=[long](& docker exec $netId cat /sys/class/net/dtcctvifb/statistics/rx_bytes)
                $aggregate=[long](($wireLast-$wireFirst)*8/($t-$wireAt))
                $result['aggregate_baseline_observation']=@{first_bytes=$wireFirst;last_bytes=$wireLast;first_t=$wireAt;last_t=$t;method='수신 IFB의 실제 네트워크 바이트·헤더 포함';rate_80_percent_bps=[long][math]::Floor($aggregate*4/5)}
                Save-Event impairment-start-before $clock
                if($Condition -eq 'N5'){Control apply N5 $aggregate}elseif($Condition -notin @('N0','N6')){Control apply $Condition}
                $phaseStart=(Get-Clock).t
                Save-Event impairment-start (Get-Clock)
                $result['aggregate_baseline_bps']=$aggregate
                $result['phase_start_t']=$phaseStart
            }
            if($null -ne $phaseStart -and $Condition -eq 'N6' -and -not $outageStarted -and $t -ge $phaseStart+60)
            {
                Save-Event outage-before $clock
                $argsLine="exec -e DTCCTV_NETEM_OWNED=1 -e DTCCTV_INTERFACE=eth0 $netId /bin/sh /control.sh apply N6"
                $n6=Start-DTCctvOwnedProcess -File $docker -Arguments $argsLine -WorkingDirectory $repo -LogPrefix "$case\outage" -Owned $owned
                $outageStarted=$true
            }
            if($n6 -and $n6.Process.HasExited -and -not $result.Contains('outage_exit')){$result['outage_exit']=$n6.Process.ExitCode;Save-Event outage-restored-observed $clock}
            if($null -ne $phaseStart -and -not $cleared -and $t -ge $phaseStart+132)
            {
                Control status
                & docker exec $netId ss -tin *> "$case\ss-before-clear.log"
                Save-Event impairment-clear-before $clock
                Control clear
                $result['clear_t']=(Get-Clock).t
                Save-Event impairment-cleared (Get-Clock)
                $cleared=$true
            }
        }
        if([datetime]::UtcNow -gt $deadline){throw '직렬 장애 측정 종료 제한 초과'}
        Start-Sleep -Milliseconds 350
    }
    Assert-DTCctvMeasurementIdle -Owned $owned -Force
    $result['native_exit_code']=$reader.Process.ExitCode
    $result.status=if($PrepareOnly){'PREPARED'}else{'MEASURED'}
    if(-not $PrepareOnly -and ($null -eq $phaseStart -or -not $cleared)){throw '장애 적용/해제 구간 미완료'}
    if($n6 -and $n6.Process.ExitCode -ne 0){throw '10초 단절 제어 실패'}
}
catch
{
    $result.note=$_.Exception.Message
    if($_.Exception -is [System.OperationCanceledException]){$result.status='INVALID'}
    elseif($result.status -ne 'ENV_BLOCKED'){$result.status='FAIL'}
}
finally
{
    if($Platform -eq 'linux' -and $Transport -eq 'udp'){& docker exec $netId node /runtime.js stop-udp-relay *> "$case\udp-relay-stop.log"}
    if($Platform -eq 'windows'){& docker exec $netId node /runtime.js stop-relay *> "$case\relay-stop.log"}
    if($initialized)
    {
        & docker exec $netId /sbin/tc -s qdisc show dev dtcctvifb *> "$case\qdisc-final.log"
        & docker exec $netId nstat -asz *> "$case\nstat-after.log"
        try{Control cleanup}catch{$result.status='FAIL';$result.note+=' '+$_.Exception.Message}
    }
    if($container){& docker rm -f $container *> "$case\container-cleanup.log";if($LASTEXITCODE -ne 0){$result.status='FAIL';$result.note+=' 소유 플레이어 컨테이너 정리 실패'}}
    try{Close-DTCctvOwnedProcesses $owned}catch{$result.status='FAIL';$result.note+=' '+$_.Exception.Message}
    $result['finished_at']=Get-Date -Format o
    Write-DTCctvJson "$case\execution.json" $result
    "$(Get-Date -Format o) | $Tag 실행 완료 | $($result.status) | $($result.note)" | Add-Content -Encoding UTF8 (Join-Path (Split-Path $ReportRoot) 'progress.md')
    $lock.ReleaseMutex();$lock.Dispose()
}
$result | ConvertTo-Json -Depth 6
