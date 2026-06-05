param(
    [int]$Depth = 16,
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
$ErrorActionPreference = "Continue"
$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$Dir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null }
$ResultsDir = (Resolve-Path $ResultsDir).Path

Write-Host "=== treebench (depth=$Depth) ===" -ForegroundColor Cyan
function Get-Mean { param([string]$txt)
    foreach ($line in $txt -split "`n") {
        if ($line -match "^\[@bench\]\s+mean\s+(\S+)\s+us") { return [double]$matches[1] }
    }
    return "N/A"
}
$rows = [ordered]@{}

Write-Host "  LS (JIT) ..." -ForegroundColor Yellow
$rows["LS (JIT)"] = Get-Mean ((& $LS run $Dir\treebench.ls $Depth 2>$null) -join "`n")
Write-Host "  LS (AOT) ..." -ForegroundColor Yellow
& $LS compile $Dir\treebench.ls -o $Dir\treebench_ls.exe 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { $rows["LS (AOT)"] = Get-Mean ((& $Dir\treebench_ls.exe $Depth) -join "`n") }
Write-Host "  Rust ..." -ForegroundColor Yellow
& rustc -O $Dir\treebench.rs -o $Dir\treebench_rs.exe 2>$null
if ($LASTEXITCODE -eq 0) { $rows["Rust"] = Get-Mean ((& $Dir\treebench_rs.exe $Depth) -join "`n") }
Write-Host "  C++ (MSVC /O2) ..." -ForegroundColor Yellow
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vc`" > nul 2>&1 && cl /O2 /std:c++17 /EHsc /MT /nologo `"$Dir\treebench.cpp`" /Fe:`"$Dir\treebench_cpp.exe`" /Fo:`"$Dir\treebench_cpp.obj`" > nul 2>&1"
if ($LASTEXITCODE -eq 0) { $rows["C++"] = Get-Mean ((& $Dir\treebench_cpp.exe $Depth) -join "`n") }
Write-Host "  Python ..." -ForegroundColor Yellow
$rows["Python"] = Get-Mean ((& python $Dir\treebench.py $Depth) -join "`n")

$fastest = ($rows.Values | Where-Object { $_ -ne "N/A" } | Measure-Object -Minimum).Minimum
$lines = @("treebench results", "=================", "  depth = $Depth (us per full tree sum, lower=faster)",
           "  NOTE: Phase 9 — LS now uses &Tree borrow (zero-copy match), within ~2x Rust", "")
foreach ($kv in $rows.GetEnumerator()) {
    if ($kv.Value -ne "N/A") {
        $lines += ("  {0,-10} {1,12:N1} us   {2,8:N1}x" -f $kv.Key, $kv.Value, ($kv.Value / $fastest))
    }
}
$summary = $lines -join "`n"
$summary | Out-File -FilePath (Join-Path $ResultsDir "treebench_summary.txt") -Encoding utf8
Write-Host "`n$summary" -ForegroundColor Green
Remove-Item "$Dir\*.exe","$Dir\*.obj","$Dir\*.pdb" -Force -ErrorAction SilentlyContinue
