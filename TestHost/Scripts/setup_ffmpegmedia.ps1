param(
	[Parameter(Mandatory = $true)]
	[string]$Source
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$descriptor = Join-Path $sourcePath 'FFMPEGMedia.uplugin'
$plugins = Join-Path $repo 'TestHost\Plugins'
$destination = Join-Path $plugins 'FFMPEGMedia'

if (-not (Test-Path -LiteralPath $descriptor -PathType Leaf))
{
	throw 'FFMPEGMedia.uplugin이 있는 원본 디렉터리를 지정하십시오.'
}
if (Test-Path -LiteralPath $destination)
{
	throw '시험 복사본이 이미 있습니다. 체크포인트와 해시를 확인한 뒤 재사용하십시오.'
}

$excluded = @('.git', 'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache')
$items = @(Get-ChildItem -LiteralPath $sourcePath -Force | Where-Object { $_.Name -notin $excluded })
foreach ($item in $items)
{
	$entries = @($item)
	if ($item.PSIsContainer)
	{
		$entries += @(Get-ChildItem -LiteralPath $item.FullName -Force -Recurse)
	}
	if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -gt 0)
	{
		throw '원본의 재분석 지점이 발견됐습니다. 복사 범위를 확인해야 합니다.'
	}
}

# 빌드 출력이 원본에 생기지 않도록 시험 호스트 안에 독립 복사한다.
New-Item -ItemType Directory -Force -Path $plugins | Out-Null
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($item in $items)
{
	Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse
}

Write-Output "FFMPEGMedia 시험 복사본 준비 완료. 위치 $destination"
