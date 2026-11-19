param([switch]$Report)

$ErrorActionPreference = 'Stop'
$VerifyDir = $PSScriptRoot
$Root = Split-Path -Parent (Split-Path -Parent $VerifyDir)
$Source = Join-Path $Root 'src'
# Original 1.1.0.0 core kept in the repository for the regression comparison.
$Original = Join-Path $VerifyDir 'baseline-1.1.0.0'
$Flags = @('-std=c17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-finput-charset=UTF-8', '-DUNICODE', '-D_UNICODE')

Push-Location -LiteralPath $VerifyDir
try {
    & gcc @Flags "-I$Original" "-I$VerifyDir" (Join-Path $VerifyDir 'OldDump.c') (Join-Path $Original 'Simulation.c') -o OldDump.exe
    if ($LASTEXITCODE -ne 0) { throw 'OldDump build failed' }

    $Sources = @((Join-Path $VerifyDir 'VerifyGroups.c'), (Join-Path $Source 'Simulation.c'), (Join-Path $Source 'Group.c'))
    $Extra = @()
    if ($Report) {
        $Sources += (Join-Path $Source 'Report.c')
        $Extra = @('-DVERIFY_REPORT')
    }
    & gcc @Flags @Extra "-I$Source" "-I$VerifyDir" @Sources -o VerifyGroups.exe
    if ($LASTEXITCODE -ne 0) { throw 'VerifyGroups build failed' }

    .\OldDump.exe > old.txt
    .\VerifyGroups.exe dump plain.txt 0
    .\VerifyGroups.exe dump single.txt 1
    $Old = Get-Content old.txt
    foreach ($Name in 'plain.txt', 'single.txt') {
        $Difference = Compare-Object $Old (Get-Content $Name)
        if ($null -eq $Difference) { Write-Output "$Name matches original 1.1.0.0 core ($($Old.Count) lines)" }
        else { Write-Output "$Name DIFFERS from original:"; $Difference | Select-Object -First 10 | Format-Table | Out-String | Write-Output }
    }
    .\VerifyGroups.exe
    if ($LASTEXITCODE -ne 0) { throw 'VerifyGroups reported failures' }
}
finally {
    Pop-Location
}
