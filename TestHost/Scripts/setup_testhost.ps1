# TestHost/Plugins/DTCctv 를 저장소 루트로 연결한다.
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$plugins = Join-Path $PSScriptRoot "..\Plugins"
$link = Join-Path $plugins "DTCctv"
New-Item -ItemType Directory -Force $plugins | Out-Null
if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target $repo | Out-Null }
