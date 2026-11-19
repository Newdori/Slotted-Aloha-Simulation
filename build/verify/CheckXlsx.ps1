param([Parameter(Mandatory)][string]$Path)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Failures = 0
function Fail($Message) { $script:Failures++; Write-Output "FAIL: $Message" }

$Zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path $Path))
$Parts = @{}
foreach ($Entry in $Zip.Entries) {
    $Reader = New-Object IO.StreamReader($Entry.Open())
    $Parts[$Entry.FullName] = [xml]$Reader.ReadToEnd()
    $Reader.Close()
}
$Zip.Dispose()

# Content types and relationship targets.
$Types = $Parts['[Content_Types].xml'].Types.Override | ForEach-Object { $_.PartName.TrimStart('/') }
foreach ($Name in $Parts.Keys) {
    if (($Name -notlike '*.rels') -and ($Name -ne '[Content_Types].xml') -and ($Types -notcontains $Name)) { Fail "missing content type for $Name" }
}
foreach ($Name in $Types) { if (-not $Parts.ContainsKey($Name)) { Fail "content type without part $Name" } }
foreach ($Name in @($Parts.Keys | Where-Object { $_ -like '*.rels' })) {
    $Base = if ($Name -eq '_rels/.rels') { '' } else { ($Name -replace '_rels/', '' -replace '\.rels$', '') | Split-Path -Parent }
    foreach ($Rel in $Parts[$Name].Relationships.Relationship) {
        $Target = if ($Base) { [IO.Path]::GetFullPath((Join-Path "C:\x\$Base" $Rel.Target)).Substring(5).Replace('\', '/') } else { $Rel.Target }
        if (-not $Parts.ContainsKey($Target)) { Fail "$Name points to missing $Target" }
    }
}

# Sheet name to cell map.
$Ns = @{ s = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main' }
$Sheets = @{}
$WorkbookRels = @{}
foreach ($Rel in $Parts['xl/_rels/workbook.xml.rels'].Relationships.Relationship) { $WorkbookRels[$Rel.Id] = 'xl/' + $Rel.Target }
foreach ($Sheet in $Parts['xl/workbook.xml'].workbook.sheets.sheet) {
    $File = $WorkbookRels[$Sheet.GetAttribute('id', 'http://schemas.openxmlformats.org/officeDocument/2006/relationships')]
    $Cells = @{}
    foreach ($Cell in (Select-Xml -Xml $Parts[$File] -XPath '//s:c' -Namespace $Ns).Node) {
        $Value = if ($Cell.is) { $Cell.is.t } else { $Cell.v }
        $Cells[$Cell.r] = $Value
    }
    $Sheets[$Sheet.name] = $Cells
}
Write-Output ("sheets: " + (($Parts['xl/workbook.xml'].workbook.sheets.sheet | ForEach-Object { $_.name }) -join ', '))

# Every chart series must match the cells it references.
$ChartCount = 0
$PointCount = 0
foreach ($Name in @($Parts.Keys | Where-Object { $_ -like 'xl/charts/*' })) {
    $ChartCount++
    $Chart = $Parts[$Name]
    $Cn = @{ c = 'http://schemas.openxmlformats.org/drawingml/2006/chart' }
    foreach ($Ref in (Select-Xml -Xml $Chart -XPath '//c:numRef' -Namespace $Cn).Node) {
        if ($Ref.f -notmatch "^'(.+)'!\`$([A-Z])\`$(\d+):\`$([A-Z])\`$(\d+)$") { Fail "$Name bad formula $($Ref.f)"; continue }
        $SheetName = $Matches[1]; $Column = $Matches[2]; $First = [int]$Matches[3]; $Last = [int]$Matches[5]
        if (-not $Sheets.ContainsKey($SheetName)) { Fail "$Name references missing sheet $SheetName"; continue }
        $Count = [int]$Ref.numCache.ptCount.val
        if (($Count -ne 0) -and ($Last - $First + 1 -ne $Count)) { Fail "$Name range $($Ref.f) has $Count points" }
        foreach ($Point in @($Ref.numCache.pt)) {
            if ($null -eq $Point) { continue }
            $Cell = "$Column$($First + [int]$Point.idx)"
            $PointCount++
            if ([double]$Sheets[$SheetName][$Cell] -ne [double]$Point.v) { Fail "$Name point $($Point.idx) = $($Point.v) but $SheetName!$Cell = $($Sheets[$SheetName][$Cell])" }
        }
    }
}
Write-Output "charts: $ChartCount, chart points checked: $PointCount"

# Group results: the 합계 row of each area is the sum of its group rows; success and collision equal the area totals.
if ($Sheets.ContainsKey('그룹별 결과')) {
    $Cells = $Sheets['그룹별 결과']
    $Final = $Sheets['결과']
    $Row = 5
    $Sum = @{}
    while ($Cells.ContainsKey("A$Row") -and ($Cells["A$Row"] -in 'Alert', 'LB')) {
        $Label = $Cells["B$Row"]
        if (($Label -eq '미배정') -or ($Label -eq '영역 합계')) { Fail "그룹별 결과 row $Row still has $Label" }
        if ($Label -eq '합계') {
            foreach ($Column in 'C', 'D', 'E', 'F', 'G', 'H', 'I') {
                if ([double]$Sum[$Column] -ne [double]$Cells["$Column$Row"]) { Fail "그룹별 결과 row $Row column $Column sum $($Sum[$Column]) != $($Cells["$Column$Row"])" }
            }
            $FinalRow = if ($Cells["A$Row"] -eq 'Alert') { 52 } else { 53 }
            foreach ($Pair in @(@('E', 'B'), @('F', 'C'), @('G', 'D'), @('I', 'F'))) {
                if ([double]$Cells["$($Pair[0])$Row"] -ne [double]$Final["$($Pair[1])$FinalRow"]) { Fail "그룹별 결과 $($Pair[0])$Row != 결과 $($Pair[1])$FinalRow" }
            }
            Write-Output "group total row matches group sum and area result for $($Cells["A$Row"]) (row $Row)"
            $Sum = @{}
        }
        else {
            foreach ($Column in 'C', 'D', 'E', 'F', 'G', 'H', 'I') {
                if ($Cells["$Column$Row"] -ne '-') { $Sum[$Column] = [double]$Sum[$Column] + [double]$Cells["$Column$Row"] }
            }
        }
        $Row++
    }
}

# Every series has a name taken from a cell of the 결과 sheet.
$SeriesCount = 0
foreach ($Name in @($Parts.Keys | Where-Object { $_ -like 'xl/charts/*' })) {
    $Cn = @{ c = 'http://schemas.openxmlformats.org/drawingml/2006/chart' }
    foreach ($Series in (Select-Xml -Xml $Parts[$Name] -XPath '//c:ser' -Namespace $Cn).Node) {
        $SeriesCount++
        $Ref = $Series.tx.strRef
        if ($null -eq $Ref) { Fail "$Name series without strRef name"; continue }
        if ($Ref.f -notmatch "^'결과'!\`$([A-Z])\`$(\d+)$") { Fail "$Name bad series name formula $($Ref.f)"; continue }
        $Cell = "$($Matches[1])$($Matches[2])"
        if ($Sheets['결과'][$Cell] -ne $Ref.strCache.pt.v) { Fail "$Name series name cache '$($Ref.strCache.pt.v)' != 결과!$Cell '$($Sheets['결과'][$Cell])'" }
    }
}
Write-Output "series names checked: $SeriesCount"
# Repeat sheet: every summary mean, minimum and maximum matches the per-run rows below it.
if ($Sheets.ContainsKey('반복 실행')) {
    $Cells = $Sheets['반복 실행']
    $HeaderRow = 5
    while (-not ($Cells.ContainsKey("A$HeaderRow") -and ($Cells["A$HeaderRow"] -eq '실행'))) { $HeaderRow++; if ($HeaderRow -gt 200) { Fail 'repeat run header missing'; break } }
    $Columns = @{}
    foreach ($Column in [char[]]'CDEFGHIJKLMNOP') { if ($Cells.ContainsKey("$Column$HeaderRow")) { $Columns[$Cells["$Column$HeaderRow"].Replace("`r", '').Replace("`n", '|')] = $Column } }
    $Runs = 0
    while ($Cells.ContainsKey("A$($HeaderRow + 1 + $Runs)")) { $Runs++ }
    function Get-RunValues([string]$Key) {
        $Column = $Columns[$Key]
        if ($null -eq $Column) { Fail "repeat column missing: $Key"; return @() }
        @(for ($Index = 0; $Index -lt $Runs; $Index++) { [double]$Cells["$Column$($HeaderRow + 1 + $Index)"] })
    }
    $Checked = 0
    for ($Row = 5; $Cells.ContainsKey("A$Row") -and ($Cells["A$Row"] -in 'Alert', 'LB'); $Row++) {
        $Label = $Cells["B$Row"]
        if ($Label -like '*차이*') {
            $Base = $Label.Substring(0, $Label.IndexOf(' · '))
            $Grouped = Get-RunValues "$($Cells["A$Row"])|$Base · 그룹 적용"
            $Baseline = Get-RunValues "$($Cells["A$Row"])|$Base · 그룹 미적용"
            $Values = @(for ($Index = 0; $Index -lt $Runs; $Index++) { $Grouped[$Index] - $Baseline[$Index] })
        }
        else {
            $Values = Get-RunValues "$($Cells["A$Row"])|$Label"
        }
        if ($Values.Count -ne $Runs) { continue }
        $Mean = ($Values | Measure-Object -Average).Average
        $Minimum = ($Values | Measure-Object -Minimum).Minimum
        $Maximum = ($Values | Measure-Object -Maximum).Maximum
        if ([Math]::Abs($Mean - [double]$Cells["C$Row"]) -gt 1e-4) { Fail "repeat mean row $Row $Mean != $($Cells["C$Row"])" }
        if (($Minimum -ne [double]$Cells["E$Row"]) -or ($Maximum -ne [double]$Cells["F$Row"])) { Fail "repeat min/max row $Row" }
        $Checked++
    }
    Write-Output "repeat statistics checked: $Checked rows over $Runs runs"
}
# Result sheet comparison: difference = grouped - baseline.
if ($Sheets['결과'].ContainsKey('B58')) {
    $Cells = $Sheets['결과']
    foreach ($Base in 58, 61) {
        foreach ($Column in 'C', 'D', 'E', 'F', 'G') {
            $Expected = [double]$Cells["$Column$Base"] - [double]$Cells["$Column$($Base + 1)"]
            if ($Expected -ne [double]$Cells["$Column$($Base + 2)"]) { Fail "결과 comparison $Column$($Base + 2)" }
        }
    }
    Write-Output "comparison differences verified"
}

if (0 -eq $Failures) { Write-Output "PASS $Path" } else { Write-Output "$Failures failures in $Path"; exit 1 }
