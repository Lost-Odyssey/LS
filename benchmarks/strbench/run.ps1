param(
    [long]$N = 1000000,
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
$ErrorActionPreference = "Continue"
$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$Dir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null }
$ResultsDir = (Resolve-Path $ResultsDir).Path

Write-Host "=== strbench (n=$N) ===" -ForegroundColor Cyan
function Get-Mean { param([string]$txt)
    foreach ($line in $txt -split "`n") {
        if ($line -match "^\[@bench\]\s+mean\s+(\S+)\s+us") { return [double]$matches[1] }
    }
    return "N/A"
}
$rows = [ordered]@{}

Write-Host "  LS (JIT) ..." -ForegroundColor Yellow
$rows["LS (JIT)"] = Get-Mean ((& $LS run $Dir\strbench.ls $N 2>$null) -join "`n")
Write-Host "  LS (JIT -O) ..." -ForegroundColor Yellow
$rows["LS (JIT -O)"] = Get-Mean ((& $LS run -O $Dir\strbench.ls $N 2>$null) -join "`n")
Write-Host "  LS (AOT) ..." -ForegroundColor Yellow
& $LS compile $Dir\strbench.ls -o $Dir\strbench_ls.exe 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { $rows["LS (AOT)"] = Get-Mean ((& $Dir\strbench_ls.exe $N) -join "`n") }
Write-Host "  Rust ..." -ForegroundColor Yellow
& rustc -O $Dir\strbench.rs -o $Dir\strbench_rs.exe 2>$null
if ($LASTEXITCODE -eq 0) { $rows["Rust"] = Get-Mean ((& $Dir\strbench_rs.exe $N) -join "`n") }
Write-Host "  C++ (MSVC /O2) ..." -ForegroundColor Yellow
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vc`" > nul 2>&1 && cl /O2 /std:c++17 /EHsc /MT /nologo `"$Dir\strbench.cpp`" /Fe:`"$Dir\strbench_cpp.exe`" /Fo:`"$Dir\strbench_cpp.obj`" > nul 2>&1"
if ($LASTEXITCODE -eq 0) { $rows["C++"] = Get-Mean ((& $Dir\strbench_cpp.exe $N) -join "`n") }
Write-Host "  Python ..." -ForegroundColor Yellow
$rows["Python"] = Get-Mean ((& python $Dir\strbench.py $N) -join "`n")

$fastest = ($rows.Values | Where-Object { $_ -ne "N/A" } | Measure-Object -Minimum).Minimum
$lines = @("strbench results", "================", "  n = $N (total us all 5 methods, lower=faster)", "")
foreach ($kv in $rows.GetEnumerator()) {
    if ($kv.Value -ne "N/A") {
        $lines += ("  {0,-13} {1,12:N0} us   {2,6:N2}x" -f $kv.Key, $kv.Value, ($kv.Value / $fastest))
    }
}
$summary = $lines -join "`n"
$summary | Out-File -FilePath (Join-Path $ResultsDir "strbench_summary.txt") -Encoding utf8
Write-Host "`n$summary" -ForegroundColor Green
Remove-Item "$Dir\*.exe","$Dir\*.obj","$Dir\*.pdb" -Force -ErrorAction SilentlyContinue
