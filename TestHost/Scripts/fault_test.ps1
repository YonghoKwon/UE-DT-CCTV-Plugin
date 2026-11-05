# 직접 시작한 RTSP 서버만 중단·복구한다. 기본 실행은 계획 출력뿐이다.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$Tools,
	[Parameter(Mandatory)][string]$FFmpeg,
	[Parameter(Mandatory)][string]$Out,
	[string]$Executable = 'C:\Program Files\Epic Games\UE_5.3\Engine\Binaries\Win64\UnrealEditor.exe',
	[ValidateRange(1, 64)][int]$Count = 16,
	[ValidateRange(1, 60)][int]$OutageSeconds = 20,
	[ValidateSet('', 'disconnected')][string]$ResizeScenario = '',
	[switch]$Windowed,
	[switch]$Execute
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\DTCctvTestCommon.ps1"
if (-not $Execute)
{
	Write-Output '계획만 출력했습니다. UE 사용이 다시 허용된 뒤 -Execute로 실행하십시오. 서버 재시작 및 전 채널 복구를 검사합니다.'
	return
}

Assert-DTCctvIdle
$project = (Resolve-Path -LiteralPath "$PSScriptRoot\..\DTCctvTestHost.uproject").Path
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$run = Join-Path $Out ('fault-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $run | Out-Null
$csv = Join-Path $run 'fault.csv'
$channelCsv = Join-Path $run 'fault.channels.csv'
$owned = [System.Collections.Generic.List[object]]::new()
$result = [ordered]@{ gate = 'fault'; status = 'FAIL'; note = ''; recovery_seconds = $null; recovery_target_seconds = 5; artifacts = $run }
$testLock = Enter-DTCctvTestLock
try
{
	$source = Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $run -Tag 'before' -Owned $owned
	$prefix = if ([IO.Path]::GetFileName($Executable) -like 'UnrealEditor*') { '"' + $project + '" -game ' } else { '' }
	$duration = 90 + $OutageSeconds
	$arguments = $prefix + '-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080 -unattended -nosplash -nosound -CctvUrl=rtsp://127.0.0.1:8554/cam -CctvCount=' + $Count + ' -CctvSeconds=' + $duration + ' -CctvCsv="' + $csv + '" -ExecCmds="r.VSync 0,t.MaxFPS 144" -abslog="' + $run + '\game.log"'
	if ($Windowed) { $arguments = $arguments.Replace('-RenderOffScreen ', '') }
	if ($ResizeScenario)
	{
		$arguments += ' -CctvResizeScenario=disconnected -CctvResizeAffected=4 -CctvShotEvery=0.5 -CctvShot="' + $run + '\disconnected.png"'
	}
	$game = Start-DTCctvOwnedProcess -File $Executable -Arguments $arguments -WorkingDirectory (Split-Path $Executable) -LogPrefix "$run\game" -Owned $owned
	$deadline = [datetime]::UtcNow.AddSeconds(45)
	do
	{
		Assert-DTCctvMeasurementIdle -Owned $owned
		if ($game.Process.HasExited) { throw '첫 재생 전에 게임이 종료됐습니다.' }
		$rows = @(if (Test-Path -LiteralPath $csv) { Import-Csv -LiteralPath $csv })
		$playing = $rows.Count -gt 0 -and [int]$rows[-1].playing -eq $Count -and [double]$rows[-1].t -ge 15
		if ($playing) { break }
		Start-Sleep -Milliseconds 200
	} while ([datetime]::UtcNow -lt $deadline)
	if (-not $playing) { throw '장애 주입 전 전 채널 재생을 확인하지 못했습니다.' }
	$before = $rows[-1]
	Save-DTCctvResources -Entry $game -Csv "$run\resources.csv"
	$stoppedAt = [datetime]::UtcNow
	Stop-DTCctvOwnedProcess $source.Server
	Stop-DTCctvOwnedProcess $source.Publisher
	Wait-DTCctvMeasurementDelay -Seconds $OutageSeconds -Owned $owned
	$source = Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $run -Tag 'after' -Owned $owned
	$restoredAt = $source.ReadyAt
	$baseline = @(Import-Csv -LiteralPath $csv)[-1]
	$baselineChannels = @(Get-DTCctvChannelSample -Csv $channelCsv -Count $Count)
	if ($baselineChannels.Count -ne $Count) { throw '채널별 CSV가 필요합니다. 최신 하네스로 패키지를 다시 빌드하십시오.' }
	$deadline = [datetime]::UtcNow.AddSeconds(30)
	do
	{
		Assert-DTCctvMeasurementIdle -Owned $owned
		if ($game.Process.HasExited) { throw '복구 전에 게임이 종료됐습니다.' }
		$rows = @(Import-Csv -LiteralPath $csv)
		$sample = $rows[-1]
		$channels = @(Get-DTCctvChannelSample -Csv $channelCsv -Count $Count)
		$advanced = @($channels | Where-Object {
			$channel = $_
			$previous = @($baselineChannels | Where-Object { $_.channel -eq $channel.channel })[0]
			[int]$channel.state -eq 2 -and [double]$channel.t -gt [double]$baseline.t -and [long]$channel.decoded -gt [long]$previous.decoded
		})
		$recovered = $advanced.Count -eq $Count
		if ($recovered)
		{
			$result.recovery_seconds = [math]::Round(([datetime]::UtcNow - $restoredAt).TotalSeconds, 3)
			break
		}
		Start-Sleep -Milliseconds 100
	} while ([datetime]::UtcNow -lt $deadline)
	if (-not $recovered) { throw '복구 후 30초 안에 전 채널 재생을 확인하지 못했습니다.' }
	Save-DTCctvResources -Entry $game -Csv "$run\resources.csv"
	$deadline = [datetime]::UtcNow.AddSeconds($duration + 30)
	while (-not $game.Process.WaitForExit(1000))
	{
		Assert-DTCctvMeasurementIdle -Owned $owned
		if ([datetime]::UtcNow -ge $deadline) { throw '게임이 지정 시험 시간 후 종료되지 않았습니다.' }
	}
	Assert-DTCctvMeasurementIdle -Owned $owned -Force
	$result.status = if ($game.Process.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }
	$result.note = '전 채널 재생과 디코딩 재개를 확인했습니다. 복구 시간은 송출 준비 로그 이후 CSV 관측값이며 표본 간격 오차가 있습니다.'
	$result['exit_code'] = $game.Process.ExitCode
	$result['outage_seconds'] = ([datetime]$restoredAt - $stoppedAt).TotalSeconds
	$result['within_recovery_target'] = $result.recovery_seconds -le 5
	$result['before_fault'] = $before
	$result['after_recovery'] = $sample
	$result['visual_status'] = if ($ResizeScenario) { 'REVIEW_REQUIRED' } else { 'NOT_APPLICABLE' }
}
catch
{
	$result.note = $_.Exception.Message
	if ($_.Exception -is [System.OperationCanceledException]) { $result.status = 'INVALID' }
	throw
}
finally
{
	try { Close-DTCctvOwnedProcesses $owned }
	catch { $result.status = 'FAIL'; $result.note += ' ' + $_.Exception.Message; throw }
	finally
	{
		try { Write-DTCctvJson -Path (Join-Path $run 'result.json') -Value $result }
		finally { $testLock.ReleaseMutex(); $testLock.Dispose() }
	}
}
Write-Output "장애 복구 시험: $($result.status), 복구 $($result.recovery_seconds)초, 결과 $run"
