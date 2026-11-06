param([Parameter(Mandatory=$true)][string]$Case,[ValidateSet('W1','W2','W3','W4','W5')][string]$Condition,[int]$PowerBits=5)
$ErrorActionPreference='Stop'
$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
. "$PSScriptRoot\DTCctvWindowNative.ps1"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$oldDpi=[DTCctvWindowNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cover=$null;$target=$null;$original=$null;$powerChanged=$false;$restored=$false
$result=[ordered]@{status='FAIL';condition=$Condition;note='';power_bits=$PowerBits;setup_actions=@();power_restore='NOT_NEEDED';samples=0}
function Write-StateJson($path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))}
function Read-GameTime
{
    if(-not(Test-Path -LiteralPath "$Case\samples.csv")){return -1.0}
    $line=Get-Content -LiteralPath "$Case\samples.csv" -Tail 1 -ErrorAction SilentlyContinue
    if($line -match '^([0-9]+\.[0-9]+),'){return [double]$matches[1]}
    return -1.0
}
try
{
    if($Condition -in @('W2','W3','W5'))
    {
        $cover=[Windows.Forms.Form]::new();$cover.Text="CCTV-01W $Condition 중립 가림 창"
        $cover.StartPosition='Manual';$cover.Location=[Drawing.Point]::new(100,100);$cover.Size=[Drawing.Size]::new(480,260)
        $label=[Windows.Forms.Label]::new();$label.Text='CCTV-01W 조건 고정용 자체 창';$label.Dock='Fill';$label.TextAlign='MiddleCenter';$cover.Controls.Add($label)
        $cover.Show();[Windows.Forms.Application]::DoEvents()
    }
    $deadline=[datetime]::UtcNow.AddSeconds(60)
    while(-not(Test-Path -LiteralPath "$Case\game.process.json"))
    {if([datetime]::UtcNow -gt $deadline){throw '게임 소유권 파일 대기 초과'};[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 100}
    $owner=Get-Content -Raw -Encoding UTF8 "$Case\game.process.json"|ConvertFrom-Json
    $target=Get-Process -Id $owner.pid
    if($target.Path -ne $owner.path -or [math]::Abs(($target.StartTime.ToUniversalTime()-[datetime]::Parse($owner.started_utc).ToUniversalTime()).TotalSeconds) -gt .05){throw '게임 PID 소유권 불일치'}
    $original=[DTCctvWindowNative]::ReadPower($target.Id);$result['power_original']=$original
    if($Condition -eq 'W5')
    {
        if(-not $original.Ok){throw '전원 제한 원래 상태를 읽지 못함'}
        $changed=[DTCctvWindowNative]::WritePower($target.Id,($original.ControlMask -bor $PowerBits),($original.StateMask -band (-bnot $PowerBits)))
        if(-not $changed.Ok){throw "전원 제한 변경 실패: $($changed.Error)"}
        $powerChanged=$true;$result['power_applied']=$changed
    }
    $stream=[IO.StreamWriter]::new("$Case\window-state.jsonl",$false,[Text.UTF8Encoding]::new($false));$stream.AutoFlush=$true
    $gpu=[IO.StreamWriter]::new("$Case\gpu.csv",$false,[Text.UTF8Encoding]::new($false));$gpu.AutoFlush=$true
    $gpu.WriteLine('utc,utilization_gpu,utilization_memory,memory_mib,pstate,sm_mhz,temperature_c,power_w')
    $previousCpu=$target.TotalProcessorTime.TotalSeconds;$previousAt=[datetime]::UtcNow;$nextGpu=$previousAt
    $deadline=[datetime]::UtcNow.AddSeconds(210)
    while(-not $target.HasExited -and [datetime]::UtcNow -lt $deadline -and -not(Test-Path -LiteralPath "$Case\monitor-stop.signal"))
    {
        [Windows.Forms.Application]::DoEvents();$target.Refresh();$t=Read-GameTime;$window=$target.MainWindowHandle
        if($t -lt 8 -and $window -ne [IntPtr]::Zero -and $Condition -ne 'W4')
        {
            if($Condition -eq 'W3')
            {
                if(-not [DTCctvWindowNative]::IsIconic($window)){$null=[DTCctvWindowNative]::ShowWindow($window,6);$result.setup_actions+=@{t=$t;action='자체 게임 최소화'}}
                if([DTCctvWindowNative]::ForegroundPid() -ne $PID){$null=[DTCctvWindowNative]::Foreground($cover.Handle)}
            }
            elseif($Condition -eq 'W1' -and [DTCctvWindowNative]::ForegroundPid() -ne $target.Id)
            {$null=[DTCctvWindowNative]::Foreground($window);$result.setup_actions+=@{t=$t;action='자체 게임 포그라운드'}}
            elseif($Condition -in @('W2','W5') -and [DTCctvWindowNative]::ForegroundPid() -ne $PID)
            {$null=[DTCctvWindowNative]::Foreground($cover.Handle);$result.setup_actions+=@{t=$t;action='자체 중립 창 포그라운드'}}
        }
        # 관측 구간은 t10 이후이며 그때부터 포커스/창 상태를 다시 조작하지 않는다.
        $wr=[DTCctvWindowNative+Rect]::new();$cr=[DTCctvWindowNative+Rect]::new();$coverRect=[DTCctvWindowNative+Rect]::new()
        if($window -ne [IntPtr]::Zero){$null=[DTCctvWindowNative]::GetWindowRect($window,[ref]$wr);$null=[DTCctvWindowNative]::GetClientRect($window,[ref]$cr)}
        if($cover){$null=[DTCctvWindowNative]::GetWindowRect($cover.Handle,[ref]$coverRect)}
        $at=[datetime]::UtcNow;$cpu=$target.TotalProcessorTime.TotalSeconds
        $pct=($cpu-$previousCpu)/[math]::Max(.001,($at-$previousAt).TotalSeconds)/[Environment]::ProcessorCount*100
        $power=[DTCctvWindowNative]::ReadPower($target.Id)
        $intersection=[math]::Max(0,[math]::Min($wr.Right,$coverRect.Right)-[math]::Max($wr.Left,$coverRect.Left))*[math]::Max(0,[math]::Min($wr.Bottom,$coverRect.Bottom)-[math]::Max($wr.Top,$coverRect.Top))
        $row=[ordered]@{utc=$at.ToString('o');game_t=$t;pid=$target.Id;foreground_pid=[DTCctvWindowNative]::ForegroundPid();cover_pid=$(if($cover){$PID}else{0});has_window=$window -ne [IntPtr]::Zero;minimized=$window -ne [IntPtr]::Zero -and [DTCctvWindowNative]::IsIconic($window);visible=$window -ne [IntPtr]::Zero -and [DTCctvWindowNative]::IsWindowVisible($window);client_w=$cr.Right;client_h=$cr.Bottom;window_rect=$wr;cover_rect=$coverRect;cover_intersection_pixels=$intersection;cpu_percent=$pct;cpu_seconds=$cpu;threads=$target.Threads.Count;private_bytes=$target.PrivateMemorySize64;power=$power}
        $stream.WriteLine(($row|ConvertTo-Json -Depth 5 -Compress));$result.samples++
        $previousCpu=$cpu;$previousAt=$at
        if($at -ge $nextGpu)
        {
            $values=& nvidia-smi --query-gpu=utilization.gpu,utilization.memory,memory.used,pstate,clocks.sm,temperature.gpu,power.draw --format=csv,noheader,nounits 2>$null
            if($LASTEXITCODE -eq 0){foreach($value in $values){$gpu.WriteLine($at.ToString('o')+','+$value)}}
            $nextGpu=$at.AddSeconds(5)
        }
        if($powerChanged -and -not $restored -and $t -ge 103)
        {
            $reset=[DTCctvWindowNative]::WritePower($target.Id,$original.ControlMask,$original.StateMask)
            $result['power_restored']=$reset;$result.power_restore=if($reset.Ok -and $reset.ControlMask -eq $original.ControlMask -and $reset.StateMask -eq $original.StateMask){'PASS'}else{'FAIL'};$restored=$result.power_restore -eq 'PASS'
            $result['power_restore_game_t']=$t
        }
        Start-Sleep -Milliseconds 750
    }
    $result.status='PASS'
}
catch{$result.note=$_.Exception.Message}
finally
{
    if($powerChanged -and -not $restored -and $target -and -not $target.HasExited)
    {$reset=[DTCctvWindowNative]::WritePower($target.Id,$original.ControlMask,$original.StateMask);$result['power_restored']=$reset;$result.power_restore=if($reset.Ok -and $reset.ControlMask -eq $original.ControlMask -and $reset.StateMask -eq $original.StateMask){'PASS'}else{'FAIL'}}
    elseif($powerChanged -and -not $restored){$result.power_restore='PROCESS_EXITED'}
    if($stream){$stream.Dispose()};if($gpu){$gpu.Dispose()};if($cover){$cover.Close();$cover.Dispose()}
    if($oldDpi -ne [IntPtr]::Zero){$null=[DTCctvWindowNative]::SetThreadDpiAwarenessContext($oldDpi)}
    Write-StateJson "$Case\window-monitor-result.json" $result
}
