# 이 파일은 함수만 정의한다. 시험 실행은 별도 스크립트의 -Execute가 필요하다.
Set-StrictMode -Version Latest
$script:DTCctvLastIdleCheck = [datetime]::MinValue

function Assert-DTCctvMeasurementIdle
{
	param([System.Collections.Generic.List[object]]$Owned, [switch]$Force)
	if ($env:DTCCTV_CANCEL_FILE -and (Test-Path -LiteralPath $env:DTCCTV_CANCEL_FILE))
	{
		throw [System.OperationCanceledException]::new('작업 중단 파일을 확인하여 자체 측정을 정리합니다.')
	}
	$now = [datetime]::UtcNow
	if (-not $Force -and ($now - $script:DTCctvLastIdleCheck).TotalSeconds -lt 5) { return }
	$script:DTCctvLastIdleCheck = $now
	$ids = @($Owned | Where-Object { -not $_.Process.HasExited } | ForEach-Object { $_.Id })
	try { Assert-DTCctvIdle -OwnedIds $ids -OwnedProcesses $Owned }
	catch { throw [System.OperationCanceledException]::new('다른 작업이 겹쳐 측정 무효: ' + $_.Exception.Message) }
}

function Wait-DTCctvMeasurementDelay
{
	param([double]$Seconds, [System.Collections.Generic.List[object]]$Owned)
	$until = [datetime]::UtcNow.AddSeconds($Seconds)
	while ([datetime]::UtcNow -lt $until)
	{
		Assert-DTCctvMeasurementIdle -Owned $Owned
		Start-Sleep -Milliseconds 500
	}
}

function Enter-DTCctvTestLock
{
	$mutex = [System.Threading.Mutex]::new($false, 'Local\DTCctvCCTV01')
	try { $acquired = $mutex.WaitOne(0) }
	catch [System.Threading.AbandonedMutexException] { $acquired = $true }
	if (-not $acquired)
	{
		$mutex.Dispose()
		throw '다른 CCTV 측정 스크립트가 실행 중입니다.'
	}
	return $mutex
}

function Test-DTCctvOwnedCimProcess
{
	param($Candidate, $OwnedProcesses)
	foreach ($entry in $OwnedProcesses)
	{
		if ($Candidate.ProcessId -ne $entry.Id -or $null -eq $entry.Started -or $null -eq $Candidate.CreationDate) { continue }
		if ($Candidate.ExecutablePath -eq $entry.Path -and
			[math]::Abs(($Candidate.CreationDate.ToUniversalTime() - $entry.Started).TotalMilliseconds) -le 1)
		{
			return $true
		}
	}
	return $false
}

function Assert-DTCctvIdle
{
	param([int[]]$OwnedIds = @(), $OwnedProcesses = @())
	$busy = @(Get-CimInstance Win32_Process | Where-Object {
		$_.ProcessId -ne $PID -and $_.ProcessId -notin $OwnedIds -and $_.Name -notmatch '^UnrealTraceServer(\.exe)?$' -and (
			$_.Name -match '^(Unreal.*|ShaderCompileWorker|DTCctvTestHost|mediamtx|ffmpeg)(\.exe)?$' -or
			$_.CommandLine -match '(UnrealBuildTool\.(dll|exe)|[\\/]Build\.bat|[\\/]RunUAT\.bat)') -and
			-not (Test-DTCctvOwnedCimProcess -Candidate $_ -OwnedProcesses $OwnedProcesses)
	})
	if ($busy.Count -gt 0)
	{
		$labels = ($busy | ForEach-Object { '{0} (PID {1})' -f $_.Name, $_.ProcessId }) -join ', '
		throw "다른 UE·시험 작업이 실행 중이므로 측정을 시작하지 않습니다: $labels"
	}
}

function Start-DTCctvOwnedProcess
{
	param([string]$File, [string]$Arguments, [string]$WorkingDirectory, [string]$LogPrefix,
		[System.Collections.Generic.List[object]]$Owned)
	$resolved = (Resolve-Path -LiteralPath $File -ErrorAction Stop).Path
	$launch = @{
		FilePath = $resolved; ArgumentList = $Arguments; WorkingDirectory = $WorkingDirectory
		RedirectStandardOutput = "$LogPrefix.stdout.log"; RedirectStandardError = "$LogPrefix.stderr.log"; PassThru = $true
	}
	if ([IO.Path]::GetFileNameWithoutExtension($resolved) -in @('mediamtx', 'ffmpeg', 'python', 'python3', 'pythonw', 'docker', 'UnrealEditor-Cmd', 'nvidia-smi'))
	{
		$launch['WindowStyle'] = 'Hidden'
	}
	$process = Start-Process @launch
	# 원래 프로세스의 핸들과 생성 시각을 보관해 PID 재사용으로 다른 작업을 종료하지 않는다.
	$entry = [pscustomobject]@{ Process = $process; Id = $process.Id; Started = $null; Path = $resolved }
	$Owned.Add($entry)
	$null = $process.Handle
	$entry.Started = $process.StartTime.ToUniversalTime()
	Write-DTCctvJson -Path "$LogPrefix.process.json" -Value @{
		pid = $entry.Id; started_utc = $entry.Started.ToString('o'); path = $entry.Path
	}
	return $entry
}

function Stop-DTCctvOwnedProcess
{
	param($Entry)
	if ($null -eq $Entry -or $Entry.Process.HasExited) { return }
	$current = Get-Process -Id $Entry.Id -ErrorAction SilentlyContinue
	if ($null -eq $current) { return }
	try
	{
		if ($null -eq $Entry.Started -or $current.StartTime.ToUniversalTime() -ne $Entry.Started -or $current.Path -ne $Entry.Path)
		{
			if ($Entry.Process.HasExited -or $current.HasExited) { return }
			throw "PID $($Entry.Id)의 소유권을 확인할 수 없어 종료하지 않았습니다."
		}
		Stop-Process -Id $Entry.Id -Force -ErrorAction Stop
		$Entry.Process.WaitForExit()
	}
	catch
	{
		# 조회 도중 스스로 종료한 관측기는 정리 실패가 아니다. 살아 있는 다른 PID는 계속 거부한다.
		if ($Entry.Process.HasExited -or $null -eq (Get-Process -Id $Entry.Id -ErrorAction SilentlyContinue)) { return }
		throw
	}
}

function Close-DTCctvOwnedProcesses
{
	param([System.Collections.Generic.List[object]]$Owned)
	$failures = @()
	for ($index = $Owned.Count - 1; $index -ge 0; --$index)
	{
		try { Stop-DTCctvOwnedProcess $Owned[$index] }
		catch { $failures += $_.Exception.Message }
		finally { $Owned[$index].Process.Dispose() }
	}
	if ($failures.Count -gt 0) { throw ($failures -join '; ') }
}

function Write-DTCctvJson
{
	param([string]$Path, $Value)
	$json = $Value | ConvertTo-Json -Depth 12
	[System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function Get-DTCctvScreenshotCheck
{
	param([string]$Path)
	# 게임 종료 뒤 PNG를 읽는다. GPU나 외부 프로그램을 실행하지 않는다.
	Add-Type -AssemblyName System.Drawing
	$image = [System.Drawing.Bitmap]::new($Path)
	try
	{
		$stepX = [math]::Max(1, [int][math]::Floor($image.Width / 64))
		$stepY = [math]::Max(1, [int][math]::Floor($image.Height / 64))
		$samples = 0
		$nonBlack = 0
		for ($y = 0; $y -lt $image.Height; $y += $stepY)
		{
			for ($x = 0; $x -lt $image.Width; $x += $stepX)
			{
				$pixel = $image.GetPixel($x, $y)
				++$samples
				if ($pixel.R -ne 0 -or $pixel.G -ne 0 -or $pixel.B -ne 0) { ++$nonBlack }
			}
		}
		return [pscustomobject]@{
			path = $Path; width = $image.Width; height = $image.Height
			sampled_pixels = $samples; non_black_pixels = $nonBlack; non_black = $nonBlack -gt 0
		}
	}
	finally { $image.Dispose() }
}

function Get-DTCctvChannelSample
{
	param([string]$Csv, [int]$Count)
	if (-not (Test-Path -LiteralPath $Csv)) { return }
	$groups = @(Import-Csv -LiteralPath $Csv | Group-Object t)
	for ($index = $groups.Count - 1; $index -ge 0; --$index)
	{
		$rows = @($groups[$index].Group)
		if ($rows.Count -eq $Count -and @($rows.channel | Sort-Object -Unique).Count -eq $Count)
		{
			return $rows
		}
	}
}

function Save-DTCctvResources
{
	param($Entry, [string]$Csv)
	if ($Entry.Process.HasExited) { return }
	$process = Get-Process -Id $Entry.Id -ErrorAction Stop
	$dedicated = $null
	$shared = $null
	try
	{
		$gpu = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -ErrorAction Stop |
			Where-Object { $_.Name -like "pid_$($Entry.Id)_*" })
		if ($gpu.Count -gt 0)
		{
			$dedicated = ($gpu | Measure-Object DedicatedUsage -Sum).Sum
			$shared = ($gpu | Measure-Object SharedUsage -Sum).Sum
		}
	}
	catch { } # WDDM 계측을 제공하지 않는 환경은 빈 값으로 기록한다.
	[pscustomobject]@{
		utc = [datetime]::UtcNow.ToString('o'); pid = $Entry.Id
		private_bytes = $process.PrivateMemorySize64; working_set_bytes = $process.WorkingSet64
		handles = $process.HandleCount; threads = $process.Threads.Count
		gpu_dedicated_bytes = $dedicated; gpu_shared_bytes = $shared
	} | Export-Csv -LiteralPath $Csv -NoTypeInformation -Append -Encoding UTF8
}

function Start-DTCctvSource
{
	param([string]$Tools, [string]$FFmpeg, [string]$Out, [string]$Tag,
		[int]$Fps = 60, [string]$Path = 'cam', [int]$Width = 1920, [int]$Height = 1080,
		[System.Collections.Generic.List[object]]$Owned, [switch]$PublisherOnly, [string]$ServerLogPrefix)
	$server = $null
	if (-not $PublisherOnly)
	{
		$ServerLogPrefix = Join-Path $Out "$Tag.server"
		# 게시 준비 메시지는 info 수준이다. 자식만 재정의하고 원본 설정 파일은 바꾸지 않는다.
		$previousLevel = [System.Environment]::GetEnvironmentVariable('MTX_LOGLEVEL', 'Process')
		$previousDestinations = [System.Environment]::GetEnvironmentVariable('MTX_LOGDESTINATIONS', 'Process')
		try
		{
			[System.Environment]::SetEnvironmentVariable('MTX_LOGLEVEL', 'info', 'Process')
			[System.Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS', 'stdout', 'Process')
			$server = Start-DTCctvOwnedProcess -File (Join-Path $Tools 'mediamtx.exe') -Arguments 'mediamtx.yml' `
				-WorkingDirectory $Tools -LogPrefix $ServerLogPrefix -Owned $Owned
		}
		finally
		{
			[System.Environment]::SetEnvironmentVariable('MTX_LOGLEVEL', $previousLevel, 'Process')
			[System.Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS', $previousDestinations, 'Process')
		}
		Start-Sleep -Seconds 2
		if ($server.Process.HasExited) { throw '직접 시작한 RTSP 서버가 종료됐습니다. 포트 충돌 여부를 확인하십시오.' }
	}
	$arguments = "-hide_banner -loglevel error -re -f lavfi -i testsrc2=size=$($Width)x$($Height):rate=$Fps -c:v h264_nvenc -preset p1 -tune ll -bf 0 -g $Fps -b:v 6M -pix_fmt yuv420p -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/$Path"
	$publisher = Start-DTCctvOwnedProcess -File $FFmpeg -Arguments $arguments -WorkingDirectory $Tools `
		-LogPrefix (Join-Path $Out "$Tag.publisher") -Owned $Owned
	$deadline = [datetime]::UtcNow.AddSeconds(20)
	do
	{
		if ($publisher.Process.HasExited) { throw '직접 시작한 시험 송출기가 종료됐습니다.' }
		$ready = @(Select-String -LiteralPath "$ServerLogPrefix.stdout.log","$ServerLogPrefix.stderr.log" `
			-Pattern "is publishing to path '$Path'" -SimpleMatch -ErrorAction SilentlyContinue).Count -gt 0
		if ($ready) { break }
		Start-Sleep -Milliseconds 100
	} while ([datetime]::UtcNow -lt $deadline)
	if (-not $ready) { throw '시험 경로의 송출 준비 로그를 확인하지 못했습니다.' }
	return [pscustomobject]@{ Server = $server; Publisher = $publisher; ReadyAt = [datetime]::UtcNow; ServerLogPrefix = $ServerLogPrefix }
}
