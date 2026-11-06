param([ValidateSet('W1','W2','W3','W4','W5')][string]$Condition,[string]$Tag,[string]$ReportRoot='', [int]$PowerBits=5,
    [string]$PackageExe='', [ValidateSet(-1,0,1)][int]$IgnoreTimerThrottling=-1)
$ErrorActionPreference='Stop'
$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $repo
. "$PSScriptRoot\DTCctvTestCommon.ps1"
if(-not $ReportRoot){$ReportRoot="$repo\Saved\Reports\CCTV-01W"}
if(-not $Tag){$Tag=$Condition.ToLower()+'-a1'}
if($Tag -notmatch '^[a-z0-9-]+$'){throw '태그 형식 오류'}
$case=Join-Path $ReportRoot "measurements\$Tag"
if(Test-Path -LiteralPath $case){throw '기존 자료를 덮어쓰지 않습니다.'}
$waitStarted=[datetime]::UtcNow
while($true)
{
    try{Assert-DTCctvIdle;break}
    catch
    {
        "$(Get-Date -Format o) | $Condition | NOT_RUN | 다른 작업 대기: $($_.Exception.Message)"|Add-Content -Encoding UTF8 "$ReportRoot\progress.md"
        if(([datetime]::UtcNow-$waitStarted).TotalMinutes -ge 30){throw 'BLOCKED_BY_OTHER_WORK'}
        $next=[datetime]::UtcNow.AddMinutes(5);while([datetime]::UtcNow -lt $next){Start-Sleep -Seconds 5}
    }
}
New-Item -ItemType Directory -Force -Path $case|Out-Null
$desktop=& "$PSScriptRoot\check_input_desktop.ps1" -Out "$case\input-desktop.json"|ConvertFrom-Json
if($desktop.status -ne 'PASS')
{
    Write-DTCctvJson "$case\execution.json" @{status='ENV_BLOCKED';tag=$Tag;condition=$Condition;note='입력 데스크톱이 Default가 아니거나 조회할 수 없음';input_desktop=$desktop}
    "$(Get-Date -Format o) | $Tag | ENV_BLOCKED | OpenInputDesktop 잠금 해제 확인 실패"|Add-Content -Encoding UTF8 "$ReportRoot\progress.md"
    return
}
$lock=Enter-DTCctvTestLock
$owned=[System.Collections.Generic.List[object]]::new()
$monitor=$null
$result=[ordered]@{status='FAIL';condition=$Condition;tag=$Tag;started_at=(Get-Date -Format o);source_cpp=(& git log -1 --format=%H -- Source TestHost/Source).Trim();note='';requested_observation_seconds=90;game_seconds=105;warmup_seconds=10;game_fps_limit=60;source_fps=60;channels=16;ui_automation_calls=0;power_bits=$PowerBits;ignore_timer_throttling=$IgnoreTimerThrottling}
try
{
    $c=Get-Content -Raw -Encoding UTF8 "$ReportRoot\checkpoint.json"|ConvertFrom-Json
    $serverConfig=Join-Path $case 'mediamtx-test.yml'
    [IO.File]::WriteAllText($serverConfig,"logLevel: info`nlogDestinations: [stdout]`nrtsp: yes`nrtspTransports: [tcp, udp]`nrtspAddress: :8554`nrtmp: no`nhls: no`nwebrtc: no`nsrt: no`npaths:`n  all_others:`n",[Text.UTF8Encoding]::new($false))
    $source=Start-DTCctvSource -Tools $c.tools.mediamtx -FFmpeg $c.tools.ffmpeg -Out $case -Tag source -Fps 60 -Owned $owned -ServerConfig $serverConfig
    $powershell=(Get-Command powershell.exe).Source
    $monitorArgs='-NoProfile -STA -ExecutionPolicy Bypass -File "'+$PSScriptRoot+'\collect_window_state.ps1" -Case "'+$case+'" -Condition '+$Condition+' -PowerBits '+$PowerBits
    $process=Start-Process -FilePath $powershell -ArgumentList $monitorArgs -WindowStyle Hidden -WorkingDirectory $repo -RedirectStandardOutput "$case\monitor.stdout.log" -RedirectStandardError "$case\monitor.stderr.log" -PassThru
    $null=$process.Handle
    $monitor=[pscustomobject]@{Process=$process;Id=$process.Id;Path=$powershell;Started=$process.StartTime.ToUniversalTime()};$owned.Add($monitor)
    Write-DTCctvJson "$case\monitor.process.json" @{pid=$monitor.Id;path=$monitor.Path;started_utc=$monitor.Started.ToString('o')}
    $exe=$PackageExe
    if(-not $exe){$exe='C:\Users\ho270\AppData\Local\Temp\cctv\pkg-J\Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe'}
    $result['package_exe']=$exe
    $result['package_sha256']=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $arguments='-windowed -ForceRes -ResX=1920 -ResY=1080 -unattended -nosplash -nosound -CctvUrl=rtsp://127.0.0.1:8554/cam -CctvCount=16 -CctvSeconds=105 -CctvCsv="'+$case+'\samples.csv" -ExecCmds="r.VSync 0,t.MaxFPS 60" -abslog="'+$case+'\game.log"'
    if($Condition -eq 'W4'){$arguments+=' -RenderOffScreen'}
    if($Condition -ne 'W3'){$arguments+=' -CctvShot="'+$case+'\final.png"'}
    if($IgnoreTimerThrottling -ge 0){$arguments+=' -DTCctvIgnoreTimerThrottling='+$IgnoreTimerThrottling}
    $desktop=& "$PSScriptRoot\check_input_desktop.ps1" -Out "$case\input-desktop-before-launch.json"|ConvertFrom-Json
    if($desktop.status -ne 'PASS'){$result.status='ENV_BLOCKED';throw 'ENV_BLOCKED: 실행 직전 입력 데스크톱 확인 실패'}
    $game=Start-DTCctvOwnedProcess -File $exe -Arguments $arguments -WorkingDirectory (Split-Path $exe) -LogPrefix "$case\game" -Owned $owned
    $result['pid']=$game.Id
    $c|Add-Member -MemberType NoteProperty -Name active_measurement -Value @{tag=$Tag;condition=$Condition;pid=$game.Id;started_at=$result.started_at} -Force
    Write-DTCctvJson "$ReportRoot\checkpoint.json" $c
    $deadline=[datetime]::UtcNow.AddSeconds(210)
    while(-not $game.Process.WaitForExit(1000))
    {
        Assert-DTCctvMeasurementIdle -Owned $owned
        if([datetime]::UtcNow -gt $deadline){throw '측정 패키지 종료 제한 시간 초과'}
        if($monitor.Process.HasExited){throw '창 상태 관측기가 먼저 종료됨'}
    }
    $result['exit_code']=$game.Process.ExitCode
    if($game.Process.ExitCode -ne 0){throw '패키지 비정상 종료'}
    if(-not $monitor.Process.WaitForExit(5000)){New-Item -ItemType File -Force -Path "$case\monitor-stop.signal"|Out-Null;$null=$monitor.Process.WaitForExit(3000)}
    $result.status='MEASURED'
}
catch{$result.note=$_.Exception.Message;if($_.Exception -is [System.OperationCanceledException]){$result.status='INVALID'}}
finally
{
    New-Item -ItemType File -Force -Path "$case\monitor-stop.signal"|Out-Null
    if($monitor -and -not $monitor.Process.HasExited){$null=$monitor.Process.WaitForExit(4000)}
    try{Close-DTCctvOwnedProcesses $owned}catch{$result.status='FAIL';$result.note+=$_.Exception.Message}
    $result['finished_at']=Get-Date -Format o
    Write-DTCctvJson "$case\execution.json" $result
    $lock.ReleaseMutex();$lock.Dispose()
}
$result|ConvertTo-Json -Depth 5
