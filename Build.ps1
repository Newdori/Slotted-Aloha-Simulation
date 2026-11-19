param([string]$DistributionDir = (Join-Path $PSScriptRoot 'excute'))

$ErrorActionPreference = 'Stop'

$CompilerPath = (Get-Command gcc.exe -ErrorAction Stop).Source
$ResourceCompilerPath = Join-Path (Split-Path -Parent $CompilerPath) 'windres.exe'
$CompilerTarget = & $CompilerPath -dumpmachine
if (($LASTEXITCODE -ne 0) -or ($CompilerTarget -ne 'x86_64-w64-mingw32')) {
    throw 'The build requires x64 MinGW-w64 GCC on PATH.'
}
if (-not (Test-Path -LiteralPath $ResourceCompilerPath -PathType Leaf)) {
    throw 'windres.exe must be installed alongside MinGW-w64 GCC.'
}

$IntermediateDir = Join-Path $PSScriptRoot 'build'
$SourceDir = Join-Path $PSScriptRoot 'src'
$DocumentDir = Join-Path $PSScriptRoot 'doc'
if (-not [System.IO.Path]::IsPathRooted($DistributionDir)) {
    $DistributionDir = Join-Path $PSScriptRoot $DistributionDir
}
$DistributionDir = [System.IO.Path]::GetFullPath($DistributionDir)
$ResourcePath = Join-Path $IntermediateDir 'Application.o'
$BuildPath = Join-Path $IntermediateDir 'SlottedAlohaSimulation.exe'
$ArchivePath = Join-Path $DistributionDir 'SlottedAlohaSimulation-Windows-x64.zip'
$SourceArchivePath = Join-Path $DistributionDir 'SlottedAlohaSimulation-Source.zip'
$ReadmePath = Join-Path $DocumentDir 'README.md'
$BuildScriptPath = Join-Path $PSScriptRoot 'Build.ps1'

New-Item -ItemType Directory -Force -Path $IntermediateDir, $DistributionDir | Out-Null
Push-Location -LiteralPath $SourceDir
try {
    & $ResourceCompilerPath --codepage=65001 --output-format=coff (Join-Path $SourceDir 'Application.rc') $ResourcePath
    if ($LASTEXITCODE -ne 0) {
        throw 'Windows resource compilation failed.'
    }

    $CompilerArguments = @(
        '-std=c17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
        '-DUNICODE', '-D_UNICODE', '-finput-charset=UTF-8', '-municode', '-mwindows', '-static',
        (Join-Path $SourceDir 'Main.c'), (Join-Path $SourceDir 'Chart.c'),
        (Join-Path $SourceDir 'Simulation.c'), (Join-Path $SourceDir 'Group.c'),
        (Join-Path $SourceDir 'GroupDialog.c'), (Join-Path $SourceDir 'Report.c'),
        $ResourcePath, '-o', $BuildPath,
        '-lgdi32', '-luser32', '-lcomdlg32', '-lcomctl32'
    )
    & $CompilerPath @CompilerArguments
    if ($LASTEXITCODE -ne 0) {
        throw 'C compilation failed.'
    }

    Compress-Archive -LiteralPath @($BuildPath, $ReadmePath) -DestinationPath $ArchivePath -Force
    Compress-Archive -LiteralPath @($SourceDir, $DocumentDir, $BuildScriptPath) -DestinationPath $SourceArchivePath -Force
    Write-Output $BuildPath
    Write-Output $ArchivePath
    Write-Output $SourceArchivePath
}
finally {
    Pop-Location
}
