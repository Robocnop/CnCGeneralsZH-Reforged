<#
.SYNOPSIS
  Builds the online launcher and puts the game build next to it, ready to zip and hand to a friend.

.DESCRIPTION
  dist\ZHReforgedOnline\ZHReforgedOnline.exe is the launcher, self-contained so the friend needs no
  .NET; dist\ZHReforgedOnline\game\ is GeneralsMD\Run without the debug symbols and without any of
  EA's archives, which every player brings from his own Zero Hour. Build the game first
  (build.bat Release).
#>
param(
	[string] $Run = (Join-Path $PSScriptRoot "..\..\GeneralsMD\Run"),
	[string] $Out = (Join-Path $PSScriptRoot "dist\ZHReforgedOnline")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $Run "generals.exe"))) {
	throw "No generals.exe in $Run - build the game first (build.bat Release)."
}

if (Test-Path $Out) {
	Remove-Item -Recurse -Force $Out
}

dotnet publish (Join-Path $PSScriptRoot "ZHReforgedOnline.csproj") -c Release -r win-x64 --self-contained true `
	-p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=none -o $Out
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }

$game = Join-Path $Out "game"
robocopy $Run $game /E /NFL /NDL /NJH /NJS /NP `
	/XF generals.pdb place_steam_build_here.txt *.big *.log DebugLogFile*.txt dx11shaders.cache | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

$tag = (git -C $PSScriptRoot describe --tags --abbrev=0 2>$null)
$sha = (git -C $PSScriptRoot rev-parse --short=7 HEAD)
if (-not $tag) { $tag = "dev" }
Set-Content -Path (Join-Path $game "version.txt") -Value "$tag+$sha" -Encoding ascii

Write-Host "Ready: $Out ($tag+$sha)"
