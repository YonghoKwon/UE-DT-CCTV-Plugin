param([string]$Codec,[ValidateSet('Windows','LinuxSW','LinuxCUDA')][string]$Mode,[int]$Count,[string]$Tag,[int]$Seconds=95)
$ErrorActionPreference='Stop'
$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
$env:PYTHONIOENCODING='utf-8'
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $repo
. "$PSScriptRoot\DTCctvTestCommon.ps1"
$report="$repo\Saved\Reports\CCTV-01L"
$case=Join-Path $report "measurements\$Tag"
if(Test-Path -LiteralPath $case){throw '기존 측정 자료를 덮어쓰지 않습니다.'}
$startedWait=[datetime]::UtcNow
while($true)
{
    try{Assert-DTCctvIdle;break}
    catch
    {
        & python "$report\gate.py" "$Tag-wait" NOT_RUN $_.Exception.Message
        if(([datetime]::UtcNow-$startedWait).TotalMinutes -ge 30){& python "$report\gate.py" $Tag BLOCKED_BY_OTHER_WORK '다른 작업이 30분 동안 계속됨';exit 3}
        $until=[datetime]::UtcNow.AddMinutes(5);while([datetime]::UtcNow -lt $until){Start-Sleep -Seconds 5}
    }
}
$lock=Enter-DTCctvTestLock
New-Item -ItemType Directory -Force -Path $case|Out-Null
$owned=[System.Collections.Generic.List[object]]::new()
$cid=$null
$c=Get-Content -LiteralPath "$report\checkpoint.json" -Encoding UTF8 -Raw|ConvertFrom-Json
$catalog=Get-Content -LiteralPath "$report\source-catalog.json" -Encoding UTF8 -Raw|ConvertFrom-Json
$spec=@($catalog.specs|Where-Object {$_.id -eq $Codec})[0]
$result=[ordered]@{status='FAIL';codec_case=$Codec;mode=$Mode;count=$Count;tag=$Tag;source_fps=$spec.fps;source_cpp=$c.source_cpp;source_commit=(& git rev-parse HEAD).Trim();started_at=(Get-Date -Format o);note='';windows_mode='offscreen';timer_option=$false;requested_seconds=$Seconds;warmup_seconds=10;exit_code=$null}
try
{
    $cpp=(& git log -1 --format=%H -- Source TestHost/Source).Trim()
    if($cpp -ne $c.source_cpp){throw 'C++가 바뀌어 새 패키지의 코드와 일치하지 않음'}
    $c.active_measurement=[pscustomobject]@{tag=$Tag;codec=$Codec;mode=$Mode;count=$Count;started_at=$result.started_at}
    Write-DTCctvJson "$report\checkpoint.json" $c
    $config=Join-Path $case 'mediamtx-test.yml'
    [IO.File]::WriteAllText($config,"logLevel: info`nlogDestinations: [stdout]`nrtsp: yes`nrtspTransports: [tcp, udp]`nrtspAddress: :8554`nrtmp: no`nhls: no`nwebrtc: no`nsrt: no`npaths:`n  all_others:`n",[Text.UTF8Encoding]::new($false))
    $server=Start-DTCctvOwnedProcess -File (Join-Path $c.tools.mediamtx 'mediamtx.exe') -Arguments ('"'+$config+'"') -WorkingDirectory $case -LogPrefix "$case\server" -Owned $owned
    Start-Sleep -Seconds 1
    if($server.Process.HasExited){throw '자체 RTSP 서버가 종료됨'}
    $pubArgs='-hide_banner -loglevel error -stream_loop -1 -re -i "'+$spec.file+'" -map 0:v:0 -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam'
    $pub=Start-DTCctvOwnedProcess -File $c.tools.ffmpeg -Arguments $pubArgs -WorkingDirectory $case -LogPrefix "$case\publisher" -Owned $owned
    $deadline=[datetime]::UtcNow.AddSeconds(25)
    do
    {
        if($pub.Process.HasExited -or $server.Process.HasExited){throw '자체 서버 또는 파일 반복 송출기 종료'}
        $ready=@(Select-String -LiteralPath "$case\server.stdout.log","$case\server.stderr.log" -Pattern "is publishing to path 'cam'" -SimpleMatch -ErrorAction SilentlyContinue).Count -gt 0
        if($ready){break}
        Assert-DTCctvMeasurementIdle -Owned $owned
        Start-Sleep -Milliseconds 100
    }while([datetime]::UtcNow -lt $deadline)
    if(-not $ready){throw '시험 경로의 송출 준비 로그 확인 실패'}
    $common=' -unattended -nosplash -nosound -CctvCount='+$Count+' -CctvSeconds='+$Seconds+' -ExecCmds="r.VSync 0,t.MaxFPS 60"'
    if($Mode -eq 'Windows')
    {
        $exe=Join-Path $c.packages 'Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe'
        $argsLine='-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080'+$common+' -CctvUrl=rtsp://127.0.0.1:8554/cam -CctvCsv="'+$case+'\samples.csv" -CctvShot="'+$case+'\final.png" -abslog="'+$case+'\game.log"'
        $game=Start-DTCctvOwnedProcess -File $exe -Arguments $argsLine -WorkingDirectory (Split-Path $exe) -LogPrefix "$case\game" -Owned $owned
        $role='game'
    }
    else
    {
        & docker image inspect $c.image --format '{{.Id}}' *> "$case\image-id.txt"
        if($LASTEXITCODE -ne 0){throw '로컬 시험 이미지 없음. 다운로드하지 않음'}
        $rhi=if($Mode -eq 'LinuxSW'){'-nullrhi -CctvSoftware'}else{'-nullrhi'}
        $command='cat /etc/redhat-release; getconf GNU_LIBC_VERSION; exec /usr/libexec/platform-python /check/capacity_linux_observer.py --out /reports/linux-cpu.csv -- /package/DTCctvTestHost/Binaries/Linux/DTCctvTestHost '+$rhi+$common+' -userdir=/tmp/ue -CctvUrl=rtsp://host.docker.internal:8554/cam -CctvCsv=/reports/samples.csv -abslog=/reports/game.log'
        [IO.File]::WriteAllText("$case\run.sh",($command+"`n"),[Text.UTF8Encoding]::new($false))
        $saved=Join-Path $c.packages 'Linux\DTCctvTestHost\Saved'
        New-Item -ItemType Directory -Force -Path $saved|Out-Null
        $emptySaved=Join-Path $case 'package-saved';New-Item -ItemType Directory -Force -Path $emptySaved|Out-Null
        $opts=@('create','--pull=never','--label','dtcctv.task=CCTV-01L','--read-only','--tmpfs','/tmp:rw,exec,mode=1777','--mount',"type=bind,source=$($c.packages)\Linux,target=/package,readonly",'--mount',"type=bind,source=$case,target=/reports",'--mount',"type=bind,source=$emptySaved,target=/package/DTCctvTestHost/Saved",'--mount',"type=bind,source=$PSScriptRoot,target=/check,readonly",'--entrypoint','/bin/bash')
        if($Mode -eq 'LinuxCUDA'){$opts+=@('--gpus','all','-e','NVIDIA_DRIVER_CAPABILITIES=all')}
        $created=& docker @opts $c.image /reports/run.sh 2> "$case\create.log"
        if($LASTEXITCODE -ne 0 -or $created -notmatch '^[0-9a-f]{64}$'){throw '자체 시험 컨테이너 생성 실패'}
        $cid=[string]$created
        Write-DTCctvJson "$case\owned-container.json" @{id=$cid;image=$c.image;created_at=(Get-Date -Format o)}
        $docker=(Get-Command docker).Source
        $game=Start-DTCctvOwnedProcess -File $docker -Arguments "start -a $cid" -WorkingDirectory $repo -LogPrefix "$case\game" -Owned $owned
        $role='lifetime'
    }
    $nvidia=(Get-Command nvidia-smi).Source
    $null=Start-DTCctvOwnedProcess -File $nvidia -Arguments '--query-gpu=timestamp,utilization.gpu,utilization.decoder,utilization.encoder,memory.used --format=csv,noheader,nounits --loop=1' -WorkingDirectory $case -LogPrefix "$case\gpu" -Owned $owned
    Write-DTCctvJson "$case\resource-owners.json" @(@{role='publisher';record="$case\publisher.process.json"},@{role=$role;record="$case\game.process.json"})
    $python=(Get-Command python).Source
    $null=Start-DTCctvOwnedProcess -File $python -Arguments ('"'+$PSScriptRoot+'\collect_codec_resources.py" "'+$case+'\resource-owners.json" "'+$case+'\cpu.csv"') -WorkingDirectory $case -LogPrefix "$case\observer" -Owned $owned
    $deadline=[datetime]::UtcNow.AddSeconds($Seconds+90)
    while(-not $game.Process.WaitForExit(1000))
    {
        Assert-DTCctvMeasurementIdle -Owned $owned
        if([datetime]::UtcNow -gt $deadline){throw '자체 패키지 측정 종료 대기 초과'}
    }
    Assert-DTCctvMeasurementIdle -Owned $owned -Force
    $result.exit_code=$game.Process.ExitCode
    if($result.exit_code -ne 0){throw '패키지 비정상 종료'}
    $result.status='MEASURED'
}
catch
{
    $result.note=$_.Exception.Message
    if($_.Exception -is [System.OperationCanceledException]){$result.status='INVALID'}
}
finally
{
    if($cid)
    {
        & docker inspect $cid *> "$case\container-inspect.json"
        & docker rm -f $cid *> "$case\container-cleanup.log"
        if($LASTEXITCODE -ne 0){$result.status='FAIL';$result.note+=' 자체 컨테이너 정리 실패'}
    }
    try{Close-DTCctvOwnedProcesses $owned}catch{$result.status='FAIL';$result.note+=$_.Exception.Message}
    $result['finished_at']=Get-Date -Format o
    Write-DTCctvJson "$case\gate-result.json" $result
    $lock.ReleaseMutex();$lock.Dispose()
}
$result|ConvertTo-Json -Depth 6
