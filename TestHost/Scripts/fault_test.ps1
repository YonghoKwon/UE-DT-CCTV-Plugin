# RTSP 서버를 강제 종료했다가 다시 띄워 자동 복구를 확인한다.
# 사용법: fault_test.ps1 -Tools <mediamtx.exe 폴더> -FFmpeg <ffmpeg.exe 경로> -Out <결과 폴더>
param(
	[Parameter(Mandatory)][string]$Tools,
	[Parameter(Mandatory)][string]$FFmpeg,
	[Parameter(Mandatory)][string]$Out,
	[int]$Count = 16,
	[int]$OutageSeconds = 20
)

$engine = "C:\Program Files\Epic Games\UE_5.3\Engine\Binaries\Win64\UnrealEditor.exe"
$project = (Resolve-Path "$PSScriptRoot\..\DTCctvTestHost.uproject").Path
$csv = Join-Path $Out "fault.csv"
$pubArgs = "-hide_banner -loglevel error -re -f lavfi -i testsrc2=size=1920x1080:rate=30 -c:v h264_nvenc -preset p1 -tune ll -bf 0 -g 30 -b:v 4M -pix_fmt yuv420p -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam"

function Start-Server {
	Start-Process (Join-Path $Tools "mediamtx.exe") -ArgumentList "mediamtx.yml" -WorkingDirectory $Tools -WindowStyle Hidden -PassThru | Out-Null
	Start-Sleep -Seconds 2
	return Start-Process $FFmpeg -ArgumentList $pubArgs -WindowStyle Hidden -PassThru
}

Stop-Process -Name mediamtx -Force -ErrorAction SilentlyContinue
$publisher = Start-Server
Start-Sleep -Seconds 3

$gameArgs = "`"$project`" -game -windowed -ResX=1920 -ResY=1080 -unattended -nosplash -CctvUrl=rtsp://127.0.0.1:8554/cam -CctvCount=$Count -CctvSeconds=$(60 + $OutageSeconds) -CctvCsv=`"$csv`" -ExecCmds=`"r.VSync 0,t.MaxFPS 60`" -log=Fault.log"
$game = Start-Process $engine -ArgumentList $gameArgs -PassThru

Start-Sleep -Seconds 25
$killedAt = Get-Date
Stop-Process -Name mediamtx -Force
Stop-Process -Id $publisher.Id -Force -ErrorAction SilentlyContinue
Write-Output "server killed"

Start-Sleep -Seconds $OutageSeconds
$publisher = Start-Server
$restoredAt = Get-Date
Write-Output "server restored after $([int]($restoredAt - $killedAt).TotalSeconds)s"

$game.WaitForExit(180000) | Out-Null
Stop-Process -Id $publisher.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Name mediamtx -Force -ErrorAction SilentlyContinue
Write-Output "game exit: $($game.ExitCode)"
Import-Csv $csv | ForEach-Object {
	Write-Output ("{0,6} playing={1,2} fps={2,5} used={3,5}MB reconn={4,3} dropped={5} err={6}" -f $_.t, $_.playing, $_.decode_fps_avg, $_.used_mb, $_.reconnects, $_.dropped, $_.errors)
}
