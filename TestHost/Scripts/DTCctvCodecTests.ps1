# G의 파일 반복 송출과 자원 관측을 기존 단일 케이스 실행기에 연결한다.
function Start-DTCctvSource
{
	param([string]$Tools,[string]$FFmpeg,[string]$Out,[string]$Tag,[int]$Fps=60,[string]$Path='cam',
		[int]$Width=1920,[int]$Height=1080,[switch]$PublisherOnly,[string]$ServerLogPrefix,
		[System.Collections.Generic.List[object]]$Owned)
	$server=$null
	if(-not $PublisherOnly)
	{
		$ServerLogPrefix=Join-Path $Out "$Tag.server"
		$oldLevel=[Environment]::GetEnvironmentVariable('MTX_LOGLEVEL','Process')
		$oldDest=[Environment]::GetEnvironmentVariable('MTX_LOGDESTINATIONS','Process')
		try
		{
			[Environment]::SetEnvironmentVariable('MTX_LOGLEVEL','info','Process')
			[Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS','stdout','Process')
			$server=Start-DTCctvOwnedProcess -File (Join-Path $Tools 'mediamtx.exe') -Arguments 'mediamtx.yml' -WorkingDirectory $Tools -LogPrefix $ServerLogPrefix -Owned $Owned
		}
		finally
		{
			[Environment]::SetEnvironmentVariable('MTX_LOGLEVEL',$oldLevel,'Process')
			[Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS',$oldDest,'Process')
		}
		Start-Sleep -Seconds 2
		if($server.Process.HasExited){throw '직접 시작한 송출 서버가 종료됐습니다.'}
	}
	$sourceFile=if($Path -eq 'sub'){$DTCctvCodecSubFile}else{$DTCctvCodecMainFile}
	if(-not $sourceFile -or -not (Test-Path -LiteralPath $sourceFile)){throw '시험 파일 경로가 없습니다.'}
	$prefix=Join-Path $Out "$Tag.publisher"
	$arguments='-hide_banner -loglevel error -stream_loop -1 -re -i "'+$sourceFile+'" -map 0:v:0 -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/'+$Path
	$publisher=Start-DTCctvOwnedProcess -File $FFmpeg -Arguments $arguments -WorkingDirectory $Tools -LogPrefix $prefix -Owned $Owned
	$DTCctvCodecPublishers.Add([pscustomobject]@{role='publisher';record="$prefix.process.json"})
	$deadline=[datetime]::UtcNow.AddSeconds(25)
	do
	{
		if($publisher.Process.HasExited){throw '시험 파일 반복 송출기가 종료됐습니다.'}
		$ready=@(Select-String -LiteralPath "$ServerLogPrefix.stdout.log","$ServerLogPrefix.stderr.log" -Pattern "is publishing to path '$Path'" -SimpleMatch -ErrorAction SilentlyContinue).Count -gt 0
		if($ready){break}
		Assert-DTCctvMeasurementIdle -Owned $Owned
		Start-Sleep -Milliseconds 100
	}while([datetime]::UtcNow -lt $deadline)
	if(-not $ready){throw '시험 파일 경로의 송출 준비 로그를 확인하지 못했습니다.'}
	$readyAt=[datetime]::UtcNow
	$sourceId=if($Path -eq 'sub'){'C9-sub'}else{$DTCctvCodecMainId}
	$probeFile=Join-Path $DTCctvCodecReport "G2\$sourceId.rtsp-probe.json"
	if(-not (Test-Path -LiteralPath $probeFile))
	{
		$probePrefix=Join-Path $Out "$Tag.ffprobe"
		$probeArgs='-v error -rtsp_transport tcp -timeout 5000000 -analyzeduration 1000000 -probesize 1000000 -select_streams v:0 -show_streams -show_entries stream=codec_name,profile,width,height,pix_fmt,r_frame_rate,avg_frame_rate,has_b_frames,color_range,color_space -of json rtsp://127.0.0.1:8554/'+$Path
		$probe=Start-DTCctvOwnedProcess -File $DTCctvCodecFfprobe -Arguments $probeArgs -WorkingDirectory $Tools -LogPrefix $probePrefix -Owned $Owned
		if(-not $probe.Process.WaitForExit(30000)){throw 'RTSP ffprobe 제한 시간 초과'}
		if($probe.Process.ExitCode -ne 0){throw '실제 RTSP ffprobe 실행 실패'}
		Copy-Item -LiteralPath "$probePrefix.stdout.log" -Destination $probeFile
	}
	Write-DTCctvJson "$Out\$Tag.source.json" @{file=$sourceFile;path=$Path;copy_only=$true;ready_utc=$readyAt.ToString('o');probe=$probeFile}
	if($DTCctvCodecLinuxSub -and $Path -eq 'cam' -and -not $PublisherOnly)
	{
		$null=Start-DTCctvSource -Tools $Tools -FFmpeg $FFmpeg -Out $Out -Tag "$Tag-sub" -Path sub -PublisherOnly -ServerLogPrefix $ServerLogPrefix -Owned $Owned
	}
	return [pscustomobject]@{Server=$server;Publisher=$publisher;ServerLogPrefix=$ServerLogPrefix;ReadyAt=$readyAt}
}

function Start-DTCctvOwnedProcess
{
	param([string]$File,[string]$Arguments,[string]$WorkingDirectory,[string]$LogPrefix,
		[System.Collections.Generic.List[object]]$Owned)
	$isGame=[IO.Path]::GetFileName($File) -eq 'DTCctvTestHost.exe'
	$isLinux=[IO.Path]::GetFileName($File) -eq 'docker.exe' -and $Arguments.StartsWith('start -a ')
	$oldCuda=[Environment]::GetEnvironmentVariable('CUDA_VISIBLE_DEVICES','Process')
	try
	{
		if($isGame -and $DTCctvCodecForceD3d){[Environment]::SetEnvironmentVariable('CUDA_VISIBLE_DEVICES','-1','Process')}
		$entry=& $DTCctvCodecBaseStart -File $File -Arguments $Arguments -WorkingDirectory $WorkingDirectory -LogPrefix $LogPrefix -Owned $Owned
	}
	finally{[Environment]::SetEnvironmentVariable('CUDA_VISIBLE_DEVICES',$oldCuda,'Process')}
	if($isGame -or $isLinux)
	{
		$nvidia=(Get-Command nvidia-smi -ErrorAction Stop).Source
		$null=& $DTCctvCodecBaseStart -File $nvidia -Arguments '--query-gpu=timestamp,utilization.gpu,utilization.decoder,utilization.encoder,memory.used --format=csv,noheader,nounits --loop=1' -WorkingDirectory $WorkingDirectory -LogPrefix "$LogPrefix.gpu" -Owned $Owned
		$records=@($DTCctvCodecPublishers.ToArray())+@([pscustomobject]@{role=$(if($isGame){'game'}else{'lifetime'});record="$LogPrefix.process.json"})
		Write-DTCctvJson "$LogPrefix.resource-owners.json" $records
		$python=(Get-Command python -ErrorAction Stop).Source
		$cpuArgs='"'+$DTCctvCodecRepo+'\TestHost\Scripts\collect_codec_resources.py" "'+$LogPrefix+'.resource-owners.json" "'+$LogPrefix+'.cpu.csv"'
		$null=& $DTCctvCodecBaseStart -File $python -Arguments $cpuArgs -WorkingDirectory $DTCctvCodecRepo -LogPrefix "$LogPrefix.cpu-observer" -Owned $Owned
	}
	return $entry
}
