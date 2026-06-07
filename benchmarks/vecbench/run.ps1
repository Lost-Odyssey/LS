param(
    [long]$N = 10000000,
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
$ErrorActionPreference = "Continue"
$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$Dir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null }
$ResultsDir = (Resolve-Path $ResultsDir).Path

Write-Host "=== vecbench (n=$N) ===" -ForegroundColor Cyan
function Get-Mean { param([string]$txt)
    foreach ($line in $txt -split "
") {
        if ($line -match "^\[@bench\]\s+mean\s+(\S+)\s+us") { return [double]$matches[1] }
    }
    return "N/A"
}
$rows = [ordered]@{}

Write-Host "  LS (JIT) ..." -ForegroundColor Yellow
$rows["LS (JIT)"] = Get-Mean ((& $LS run $Dir\vecbench.ls $N 2>$null) -join "
")
Write-Host "  LS (JIT -O) ..." -ForegroundColor Yellow
$rows["LS (JIT -O)"] = Get-Mean ((& $LS run -O $Dir\vecbench.ls $N 2>$null) -join "
")
Write-Host "  LS (AOT) ..." -ForegroundColor Yellow
& $LS compile $Dir\vecbench.ls -o $Dir\vecbench_ls.exe 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { $rows["LS (AOT)"] = Get-Mean ((& $Dir\vecbench_ls.exe $N) -join "
") }
Write-Host "  LS Vec (JIT) ..." -ForegroundColor Yellow
$rows["LS Vec (JIT)"] = Get-Mean ((& $LS run $Dir\vecbench_ls.ls $N 2>$null) -join "
")
Write-Host "  LS Vec (JIT -O) ..." -ForegroundColor Yellow
$rows["LS Vec (JIT -O)"] = Get-Mean ((& $LS run -O $Dir\vecbench_ls.ls $N 2>$null) -join "
")
Write-Host "  LS Vec (AOT) ..." -ForegroundColor Yellow
& $LS compile $Dir\vecbench_ls.ls -o $Dir\vecbench_lsv.exe 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { $rows["LS Vec (AOT)"] = Get-Mean ((& $Dir\vecbench_lsv.exe $N) -join "
") }
Write-Host "  Rust ..." -ForegroundColor Yellow
& rustc -O $Dir\vecbench.rs -o $Dir\vecbench_rs.exe 2>$null
if ($LASTEXITCODE -eq 0) { $rows["Rust"] = Get-Mean ((& $Dir\vecbench_rs.exe $N) -join "
") }
Write-Host "  C++ (MSVC /O2) ..." -ForegroundColor Yellow
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c ""$vc" > nul 2>&1 && cl /O2 /std:c++17 /EHsc /MT /nologo "$Dir\vecbench.cpp" /Fe:"$Dir\vecbench_cpp.exe" /Fo:"$Dir\vecbench_cpp.obj" > nul 2>&1"
if ($LASTEXITCODE -eq 0) { $rows["C++"] = Get-Mean ((& $Dir\vecbench_cpp.exe $N) -join "
") }
Write-Host "  Python (1/10 N, scaled) ..." -ForegroundColor Yellow
$pyN = [long]($N / 10); if ($pyN -lt 1) { $pyN = 1 }
$pm = Get-Mean ((& python $Dir\vecbench.py $pyN) -join "
")
if ($pm -ne "N/A") { $pm = $pm * 10 }
$rows["Python (est)"] = $pm

$fastest = ($rows.Values | Where-Object { $_ -ne "N/A" } | Measure-Object -Minimum).Minimum
$lines = @("vecbench results", "================", "  n = $N (total us all 4 ops, lower=faster)", "")
foreach ($kv in $rows.GetEnumerator()) {
    if ($kv.Value -ne "N/A") {
        $lines += ("  {0,-13} {1,12:N0} us   {2,6:N2}x" -f $kv.Key, $kv.Value, ($kv.Value / $fastest))
    }
}
$summary = $lines -join "
"
$summary | Out-File -FilePath (Join-Path $ResultsDir "vecbench_summary.txt") -Encoding utf8
Write-Host "
$summary" -ForegroundColor Green
Remove-Item "\*.exe","\*.obj","\*.pdb" -Force -ErrorAction SilentlyContinue