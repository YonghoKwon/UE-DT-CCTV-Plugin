# CCTV-01의 보류 항목 4~8을 순차 실행한다. 현재는 실행 금지이며 기본 동작은 계획 출력이다.
[CmdletBinding()]
param(
	[string]$Tools = 'C:\Users\ho270\AppData\Local\Temp\cctv\dl\mtx',
	[string]$FFmpeg = '',
	[string]$Packages = 'C:\Users\ho270\AppData\Local\Temp\cctv\pkg',
	[string]$Engine = 'C:\Program Files\Epic Games\UE_5.3',
	[ValidateSet('ps-node24-rhel8-debug:20261027', 'codex-ps-cirrus:glibc251-20261027')]
	[string]$LinuxImage = 'ps-node24-rhel8-debug:20261027',
	[switch]$Execute
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\DTCctvTestCommon.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path
$out = Join-Path $repo 'Saved\Reports\CCTV-01'
if (-not $Execute)
{
	Write-Output '실행하지 않았습니다. 향후 -Execute: 변경 코드 재빌드·패키징 → 4 Windows 60/30fps → 5 Linux → 6 Pixel Streaming → 7 크기·서브 스트림 → 8 재시작·반복·60분 순서입니다.'
	return
}

Assert-DTCctvIdle
if (-not $FFmpeg)
{
	$FFmpeg = @(Get-ChildItem -LiteralPath 'C:\Users\ho270\AppData\Local\Temp\cctv\dl\ff\win' -Filter ffmpeg.exe -Recurse -File)[0].FullName
}
$null = Resolve-Path -LiteralPath $FFmpeg, (Join-Path $Tools 'mediamtx.exe')
$project = Join-Path $repo 'TestHost\DTCctvTestHost.uproject'
$descriptor = Get-Content -Raw -Encoding UTF8 -LiteralPath $project | ConvertFrom-Json
if (@($descriptor.Plugins | Where-Object { $_.Name -eq 'PixelStreaming' -and $_.Enabled }).Count -ne 1)
{
	throw 'PixelStreaming을 켠 TestHost 구성이 필요합니다.'
}
$run = Join-Path $out ('pending-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$logs = Join-Path $repo ('Saved\Logs\CCTV-01\' + (Split-Path $run -Leaf))
New-Item -ItemType Directory -Path $run, $logs -Force | Out-Null
$psScope = '플러그인 로드 공존만 확인, 실제 스트리밍 동시 부하는 미검증'
$psUserChecks = @(
	'사용자 확인: Windows·Linux에서 시그널링 서버와 클라이언트를 연결하고 실제 Pixel Streaming 영상을 수신할 것.',
	'사용자 확인: 실제 NVENC 인코딩이 활성화된 상태에서 RTSP 16채널 동시 재생과 FPS·손실·GPU 자원 추세를 확인할 것.'
)
$results = [ordered]@{ known_limits = @($psScope); user_checks = $psUserChecks }
$winExe = Join-Path $Packages 'Windows\DTCctvTestHost\Binaries\Win64\DTCctvTestHost.exe'
$linuxRendering = $false

function Invoke-DTCctvBuild
{
	# 빌드는 동기 실행하며 RTSP 송출이나 게임 실행과 겹치지 않는다.
	& "$PSScriptRoot\setup_testhost.ps1"
	$build = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
	$uat = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'
	& $build DTCctvTestHostEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload *> "$logs\editor-build.log"
	if ($LASTEXITCODE -ne 0) { throw 'Windows Editor 빌드 실패. 빌드 로그를 확인하십시오.' }
	$owned = [System.Collections.Generic.List[object]]::new()
	try
	{
		$unitDir = Join-Path $run 'unit'
		$unitArgs = '"' + $project + '" -ExecCmds="Automation RunTests DTCctv" -TestExit="Automation Test Queue Empty" -ReportExportPath="' + $unitDir + '" -nullrhi -unattended -nosplash -abslog="' + $logs + '\unit.log"'
		$unit = Start-DTCctvOwnedProcess -File (Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -Arguments $unitArgs -WorkingDirectory $repo -LogPrefix "$logs\unit" -Owned $owned
		if (-not $unit.Process.WaitForExit(180000) -or $unit.Process.ExitCode -ne 0) { throw 'DTCctv 단위 테스트 실패 또는 제한 시간 초과.' }
		$unitResult = Get-Content -Raw -Encoding UTF8 -LiteralPath "$unitDir\index.json" | ConvertFrom-Json
		if ($unitResult.succeeded -le 0 -or $unitResult.failed -ne 0 -or $unitResult.notRun -ne 0) { throw '미성공 단위 테스트가 있습니다.' }
	}
	finally { Close-DTCctvOwnedProcesses $owned }
	foreach ($platform in @('Win64', 'Linux'))
	{
		& $uat BuildCookRun "-project=$project" -noP4 "-platform=$platform" -clientconfig=Development -build -cook -stage -pak -package -archive "-archivedirectory=$Packages" -unattended -utf8output *> "$logs\package-$platform.log"
		if ($LASTEXITCODE -ne 0) { throw "$platform 패키징 실패. 기존 패키지를 최신 결과로 사용하지 않습니다." }
	}
}

function Invoke-DTCctvWindowsCase
{
	param([string]$Tag, [int]$Fps = 60, [int]$Seconds = 80, [string]$Extra = '', [switch]$SubStream, [switch]$ValidateRendering, [switch]$Windowed)
	Assert-DTCctvIdle
	$case = Join-Path $run $Tag
	New-Item -ItemType Directory -Path $case | Out-Null
	$owned = [System.Collections.Generic.List[object]]::new()
	$summary = [ordered]@{ status = 'FAIL'; note = ''; path = $case; fps = $Fps }
	try
	{
		$source = Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $logs -Tag $Tag -Fps $Fps -Owned $owned
		if ($SubStream)
		{
			$null = Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $logs -Tag "$Tag-sub" -Fps $Fps -Path sub -Width 640 -Height 360 -PublisherOnly -ServerLogPrefix $source.ServerLogPrefix -Owned $owned
			$Extra += ' -CctvSubUrl=rtsp://127.0.0.1:8554/sub'
		}
		$argsLine = '-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080 -unattended -nosplash -nosound -CctvUrl=rtsp://127.0.0.1:8554/cam -CctvCount=16 -CctvSeconds=' + $Seconds + ' -CctvCsv="' + $case + '\samples.csv" -CctvShot="' + $case + '\final.png" -ExecCmds="r.VSync 0,t.MaxFPS 144" -abslog="' + $logs + '\' + $Tag + '.game.log" ' + $Extra
		$summary['window_mode'] = if ($Windowed) { 'windowed' } else { 'offscreen' }
		if ($Windowed) { $argsLine = $argsLine.Replace('-RenderOffScreen ', '') }
		$game = Start-DTCctvOwnedProcess -File $winExe -Arguments $argsLine -WorkingDirectory (Split-Path $winExe) -LogPrefix "$logs\$Tag.game" -Owned $owned
		$deadline = [datetime]::UtcNow.AddSeconds($Seconds + 90)
		while (-not $game.Process.WaitForExit(5000))
		{
			Assert-DTCctvMeasurementIdle -Owned $owned
			Save-DTCctvResources -Entry $game -Csv "$case\resources.csv"
			if ([datetime]::UtcNow -ge $deadline) { throw '하네스가 제한 시간 후 종료되지 않았습니다.' }
		}
		Assert-DTCctvMeasurementIdle -Owned $owned -Force
		$summary['exit_code'] = $game.Process.ExitCode
		if ($game.Process.ExitCode -ne 0) { throw '패키지 실행 실패.' }
		if ($ValidateRendering)
		{
			$summary['render_validation'] = [ordered]@{ valid = $false; output_w_positive = $false; screenshot = $null }
		}
		$rows = @(Import-Csv -LiteralPath "$case\samples.csv")
		$steady = @($rows | Where-Object { [double]$_.t -ge 10 })
		if ($steady.Count -lt 2) { throw '충분한 측정 CSV가 없습니다.' }
		if ($ValidateRendering)
		{
			$summary.render_validation.output_w_positive = @($steady | Where-Object { [int]$_.output_w -le 0 }).Count -eq 0
			if (-not $summary.render_validation.output_w_positive) { throw '첫 Windows 측정의 output_w가 0이므로 측정을 무효로 기록합니다.' }
			$summary.render_validation.screenshot = Get-DTCctvScreenshotCheck -Path "$case\final.png"
			if ($summary.render_validation.screenshot.width -ne 1920 -or $summary.render_validation.screenshot.height -ne 1080)
			{
				throw '요청한 1920x1080 캡처 크기가 아니므로 측정을 무효로 기록합니다.'
			}
			if (-not $summary.render_validation.screenshot.non_black) { throw '첫 Windows 측정의 PNG가 검은 화면이므로 측정을 무효로 기록합니다.' }
			$summary.render_validation.valid = $true
		}
		$secondsMeasured = [double]$steady[-1].t - [double]$steady[0].t
		$summary['seconds'] = $secondsMeasured
		$summary['decode_fps'] = ([long]$steady[-1].decoded - [long]$steady[0].decoded) / (16 * $secondsMeasured)
		$summary['display_fps'] = ([long]$steady[-1].displayed - [long]$steady[0].displayed) / (16 * $secondsMeasured)
		$summary['dropped'] = ($rows | Measure-Object dropped -Maximum).Maximum
		$summary['reconnects'] = ($rows | Measure-Object reconnects -Maximum).Maximum
		$summary['all_playing'] = @($steady | Where-Object { [int]$_.playing -ne 16 }).Count -eq 0
		$summary['screenshots'] = @(Get-ChildItem -LiteralPath $case -Filter '*.png' -File).Count
		$summary['pixel_streaming_mounted'] = @(Select-String -LiteralPath "$logs\$Tag.game.log" -SimpleMatch 'Mounting Engine plugin PixelStreaming').Count -gt 0
		$channelRows = @(Import-Csv -LiteralPath "$case\samples.channels.csv" | Where-Object { [double]$_.t -ge 10 })
		$summary['channels'] = @($channelRows | Group-Object channel | ForEach-Object {
			[pscustomobject]@{ channel = [int]$_.Name; decode_fps_avg = ($_.Group | Measure-Object decode_fps -Average).Average; display_fps_avg = ($_.Group | Measure-Object display_fps -Average).Average }
		})
		$summary.status = 'MEASURED'
	}
	catch
	{
		$summary.note = $_.Exception.Message
		if ($_.Exception -is [System.OperationCanceledException]) { $summary.status = 'INVALID' }
		if ($ValidateRendering -and $summary.Contains('render_validation') -and -not $summary.render_validation.valid)
		{
			$summary.status = 'INVALID'
		}
	}
	finally
	{
		try { Close-DTCctvOwnedProcesses $owned }
		catch { $summary.status = 'FAIL'; $summary.note = $_.Exception.Message }
		Write-DTCctvJson -Path "$case\result.json" -Value $summary
	}
	return ,$summary
}

function Invoke-DTCctvLinuxCase
{
	param([string]$Tag, [int]$Count = 1, [switch]$Gpu, [switch]$AddHost, [switch]$NullRhiAuto, [string]$Extra = '', [string[]]$DockerOptions = @())
	Assert-DTCctvIdle
	$case = Join-Path $run $Tag
	New-Item -ItemType Directory -Path $case | Out-Null
	$owned = [System.Collections.Generic.List[object]]::new()
	$containerId = $null
	$summary = [ordered]@{ status = 'FAIL'; note = ''; path = $case; rendering = [bool]($Gpu -and -not $NullRhiAuto) }
	try
	{
		$null = Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $logs -Tag $Tag -Owned $owned
		& docker image inspect $LinuxImage --format '{{.Id}}' *> "$logs\$Tag.image.log"
		if ($LASTEXITCODE -ne 0) { throw '지정한 로컬 이미지가 없습니다. 다운로드하지 않습니다.' }
		$rhi = if ($Gpu) { '-RenderOffscreen -vulkan' } else { '-nullrhi -CctvSoftware' }
		if ($NullRhiAuto) { $rhi = '-nullrhi' }
		$command = 'cat /etc/redhat-release && getconf GNU_LIBC_VERSION && exec /package/DTCctvTestHost/Binaries/Linux/DTCctvTestHost ' + $rhi + ' -unattended -nosound -nosplash -userdir=/tmp/ue -CctvUrl=rtsp://host.docker.internal:8554/cam -CctvCount=' + $Count + ' -CctvSeconds=65 -CctvCsv=/reports/samples.csv -abslog=/reports/game.log'
		$command += ' ' + $Extra
		$dockerArgs = @('create', '--pull=never', '--read-only', '--tmpfs', '/tmp:rw,exec,mode=1777', '--mount', "type=bind,source=$Packages\Linux,target=/package,readonly", '--mount', "type=bind,source=$case,target=/reports", '--entrypoint', '/bin/bash')
		if ($Gpu) { $dockerArgs += @('--gpus', 'all') }
		if ($AddHost) { $dockerArgs += @('--add-host', 'host.docker.internal:host-gateway') }
		$dockerArgs += $DockerOptions
		$created = & docker @dockerArgs $LinuxImage -lc $command 2> "$logs\$Tag.create.log"
		if ($LASTEXITCODE -ne 0 -or $created -notmatch '^[0-9a-f]{64}$') { throw '시험 컨테이너 생성 실패.' }
		$containerId = [string]$created
		Write-DTCctvJson -Path "$case\owned-container.json" -Value @{
			id = $containerId; image = $LinuxImage; created_at = (Get-Date -Format o)
		}
		$dockerExe = (Get-Command docker -ErrorAction Stop).Source
		$client = Start-DTCctvOwnedProcess -File $dockerExe -Arguments "start -a $containerId" -WorkingDirectory $repo -LogPrefix "$logs\$Tag.docker" -Owned $owned
		$deadline = [datetime]::UtcNow.AddSeconds(120)
		while (-not $client.Process.WaitForExit(5000))
		{
			Assert-DTCctvMeasurementIdle -Owned $owned
			if ([datetime]::UtcNow -ge $deadline) { throw 'Linux 시험 제한 시간 초과.' }
		}
		Assert-DTCctvMeasurementIdle -Owned $owned -Force
		$summary['exit_code'] = $client.Process.ExitCode
		$loader = Get-Content -Raw -Encoding UTF8 -LiteralPath "$logs\$Tag.docker.stdout.log"
		$gameLog = if (Test-Path -LiteralPath "$case\game.log") { Get-Content -Raw -Encoding UTF8 -LiteralPath "$case\game.log" } else { '' }
		$summary['rhel8'] = $loader -match '(release 8\.|release 8 )'
		$summary['ffmpeg_loaded'] = $gameLog -match 'LogDTCctv: FFmpeg '
		$summary['pixel_streaming_mounted'] = $gameLog -match 'Mounting Engine plugin PixelStreaming'
		$rows = @(if (Test-Path -LiteralPath "$case\samples.csv") { Import-Csv -LiteralPath "$case\samples.csv" })
		$summary['decoded'] = if ($rows.Count) { [long]$rows[-1].decoded } else { 0 }
		$summary['all_playing'] = $rows.Count -gt 0 -and [int]$rows[-1].playing -eq $Count
		$channelRows = @(Get-DTCctvChannelSample -Csv "$case\samples.channels.csv" -Count $Count)
		$summary['channels'] = $channelRows
		$summary['cuda_channels'] = @($channelRows | Where-Object { $_.decoder -eq 'cuda' }).Count
		$summary['software_channels'] = @($channelRows | Where-Object { $_.decoder -eq 'software' }).Count
		$summary['nvdec'] = $summary.cuda_channels -gt 0
		$summary['vulkan'] = $Gpu -and $gameLog -match 'LogVulkanRHI:'
		if ($client.Process.ExitCode -eq 0 -and $summary.rhel8 -and $summary.ffmpeg_loaded -and $summary.decoded -gt 0 -and $summary.all_playing) { $summary.status = 'PASS' }
		else { $summary.note = '배포판·로더·프레임·전 채널 재생 또는 종료 코드를 충족하지 못했습니다.' }
	}
	catch
	{
		$summary.note = $_.Exception.Message
		if ($_.Exception -is [System.OperationCanceledException]) { $summary.status = 'INVALID' }
	}
	finally
	{
		if ($containerId)
		{
			# 이 호출에서 create가 반환한 컨테이너 ID만 제거한다. 기존 컨테이너·이미지는 변경하지 않는다.
			& docker rm -f $containerId *> "$logs\$Tag.cleanup.log"
			if ($LASTEXITCODE -ne 0) { $summary.status = 'FAIL'; $summary.note += ' 소유 컨테이너 정리 실패.' }
		}
		try { Close-DTCctvOwnedProcesses $owned }
		catch { $summary.status = 'FAIL'; $summary.note += $_.Exception.Message }
		Write-DTCctvJson -Path "$case\result.json" -Value $summary
	}
	return ,$summary
}

$testLock = Enter-DTCctvTestLock
try
{
	# CCTV-01A의 C++ 변경을 포함하도록 3번 패키징을 다시 수행하는 선행 조건이다.
	Invoke-DTCctvBuild
	$results['4_win60'] = Invoke-DTCctvWindowsCase -Tag win60 -ValidateRendering
	$win60 = $results['4_win60']
	$win60['gate'] = 'package_win_16ch'
	if ($win60.status -ne 'INVALID')
	{
		$win60.status = if ($win60.status -eq 'MEASURED' -and $win60.seconds -ge 60 -and $win60.decode_fps -ge 58 -and $win60.display_fps -ge 58 -and $win60.dropped -eq 0 -and $win60.reconnects -eq 0 -and $win60.all_playing) { 'PASS' } else { 'FAIL' }
	}
	$results['4_win30'] = Invoke-DTCctvWindowsCase -Tag win30 -Fps 30
	$results['5_linux_gpu'] = Invoke-DTCctvLinuxCase -Tag linux-gpu -Gpu
	$linuxRendering = $results['5_linux_gpu'].status -eq 'PASS'
	if (-not $linuxRendering)
	{
		$results['5_linux_software'] = Invoke-DTCctvLinuxCase -Tag linux-software
		if ($results['5_linux_software'].status -ne 'PASS') { $results['5_linux_add_host'] = Invoke-DTCctvLinuxCase -Tag linux-add-host -AddHost }
	}
	$results['6_ps_windows'] = Invoke-DTCctvWindowsCase -Tag ps-windows
	$ps = $results['6_ps_windows']
	$ps.status = if ($ps.status -eq 'MEASURED' -and $ps.all_playing -and $ps.pixel_streaming_mounted) { 'PASS' } else { 'FAIL' }
	$results['6_ps_linux'] = Invoke-DTCctvLinuxCase -Tag ps-linux -Count 16 -Gpu:$linuxRendering -AddHost
	$psLinux = $results['6_ps_linux']
	if ($psLinux.status -eq 'PASS' -and -not $psLinux.pixel_streaming_mounted)
	{
		$psLinux.status = 'FAIL'
		$psLinux.note = 'Linux Pixel Streaming 마운트를 확인하지 못했습니다.'
	}
	foreach ($psKey in @('6_ps_windows', '6_ps_linux'))
	{
		$psCase = $results[$psKey]
		$psCase['validation_scope'] = $psScope
		$psCase['actual_streaming_concurrent_load_status'] = 'NOT_RUN'
		$psCase['known_limits'] = @($psScope)
		$psCase['user_checks'] = $psUserChecks
		$psCase.note = ($psCase.note + ' ' + $psScope).Trim()
		Write-DTCctvJson -Path (Join-Path $psCase.path 'result.json') -Value $psCase
	}
	foreach ($scenario in @('drag', 'extremes', 'subset'))
	{
		$results["7_$scenario"] = Invoke-DTCctvWindowsCase -Tag "resize-$scenario" -Seconds 80 -Extra "-CctvResizeScenario=$scenario -CctvResizeAffected=4 -CctvShotEvery=0.5"
		# 실제 PNG 연속 관찰이 필요하므로 자동 PASS로 올리지 않는다.
		$resize = $results["7_$scenario"]
		if ($resize.status -eq 'MEASURED')
		{
			$resize.status = if ($resize.dropped -eq 0 -and $resize.reconnects -eq 0 -and $resize.screenshots -gt 2) { 'REVIEW_REQUIRED' } else { 'FAIL' }
			$resize['unchanged_channels'] = @($resize.channels | Where-Object { $_.channel -ge 4 })
		}
	}
	$results['7_substream'] = Invoke-DTCctvWindowsCase -Tag substream -Seconds 80 -SubStream -Extra '-CctvResizeScenario=extremes -CctvResizeAffected=4 -CctvShotEvery=0.5'
	if ($results['7_substream'].status -eq 'MEASURED')
	{
		$sub = $results['7_substream']
		$subRows = @(Import-Csv -LiteralPath (Join-Path $sub.path 'samples.channels.csv') | Where-Object { [int]$_.channel -lt 4 -and [double]$_.t -ge 10 })
		$sub['source_widths'] = @($subRows.src_w | Sort-Object -Unique)
		$sub.status = if ($sub.source_widths -contains '640' -and $sub.source_widths -contains '1920' -and $sub.screenshots -gt 2) { 'REVIEW_REQUIRED' } else { 'FAIL' }
	}
	try
	{
		& "$PSScriptRoot\fault_test.ps1" -Tools $Tools -FFmpeg $FFmpeg -Out $run -Executable $winExe -ResizeScenario disconnected -Execute
		$fault = @(Get-ChildItem -LiteralPath $run -Directory -Filter 'fault-*' | Sort-Object Name)[-1]
		$results['7_disconnected_8_fault'] = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $fault.FullName 'result.json') | ConvertFrom-Json
	}
	catch { $results['7_disconnected_8_fault'] = @{ status = 'FAIL'; note = $_.Exception.Message } }
	$results['8_close_open'] = Invoke-DTCctvWindowsCase -Tag close-open -Seconds 120 -Extra '-CctvCloseOpenCycles=5 -CctvCloseOpenInterval=10 -CctvCloseHold=2'
	if ($results['8_close_open'].status -eq 'MEASURED')
	{
		$cycles = $results['8_close_open']
		$events = @(Import-Csv -LiteralPath (Join-Path $cycles.path 'samples.events.csv'))
		$cycles['completed_cycles'] = @($events | Where-Object { $_.event -eq 'open' }).Count
		$cycles.status = if ($cycles.completed_cycles -eq 5) { 'REVIEW_REQUIRED' } else { 'FAIL' }
		$cycles.note = '자원 CSV의 반복 전후 메모리·핸들·스레드·GPU 메모리와 마지막 전 채널 복구를 검수해야 합니다.'
	}
	# 장시간 시험을 마지막에 실행한다. 처음 10분을 뺀 메모리 기울기와 스레드 범위를 기록한다.
	$results['8_soak'] = Invoke-DTCctvWindowsCase -Tag soak -Seconds 3615
	$soak = $results['8_soak']
	if ($soak.status -eq 'MEASURED')
	{
		$resourceRows = @(Import-Csv -LiteralPath (Join-Path $soak.path 'resources.csv'))
		$origin = [datetime]$resourceRows[0].utc
		$afterTen = @($resourceRows | Where-Object { ([datetime]$_.utc - $origin).TotalSeconds -ge 600 })
		if ($afterTen.Count -ge 2)
		{
			$hours = ([datetime]$afterTen[-1].utc - [datetime]$afterTen[0].utc).TotalHours
			$soak['private_mb_per_hour_after_10m'] = ([double]$afterTen[-1].private_bytes - [double]$afterTen[0].private_bytes) / 1MB / $hours
			$soak['threads_min'] = ($afterTen | Measure-Object threads -Minimum).Minimum
			$soak['threads_max'] = ($afterTen | Measure-Object threads -Maximum).Maximum
			$soak['handles_min'] = ($afterTen | Measure-Object handles -Minimum).Minimum
			$soak['handles_max'] = ($afterTen | Measure-Object handles -Maximum).Maximum
			$gpuRows = @($afterTen | Where-Object { $_.gpu_dedicated_bytes -ne '' })
			$soak['gpu_memory_available'] = $gpuRows.Count -eq $afterTen.Count
			if ($gpuRows.Count -ge 2)
			{
				$soak['gpu_dedicated_mb_delta_after_10m'] = ([double]$gpuRows[-1].gpu_dedicated_bytes - [double]$gpuRows[0].gpu_dedicated_bytes) / 1MB
			}
			$soak.status = if ($soak.seconds -ge 3600 -and $soak.dropped -eq 0 -and $soak.reconnects -eq 0 -and $soak.all_playing -and $soak.threads_min -eq $soak.threads_max) { 'REVIEW_REQUIRED' } else { 'FAIL' }
			$soak.note = '메모리·GPU 메모리의 지속 증가와 누수 여부는 전체 resources.csv 추세를 검수해야 합니다.'
		}
		else { $soak.status = 'FAIL'; $soak.note = '10분 이후 자원 표본이 부족합니다.' }
	}
}
catch { $results['blocked'] = @{ status = 'NOT_RUN'; note = $_.Exception.Message } }
finally
{
	try { Write-DTCctvJson -Path (Join-Path $run 'pending-results.json') -Value $results }
	finally { $testLock.ReleaseMutex(); $testLock.Dispose() }
}
Write-Output "보류 시험 순차 실행 결과: $run"
