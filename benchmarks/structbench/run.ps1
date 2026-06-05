param(
    [long]$N = 100000000,
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
# Keep "Continue": PS 5.1 turns native-exe stderr into a terminating error under
# "Stop"; every step checks $LASTEXITCODE explicitly instead.
$ErrorActionPreference = "Continue"

$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$Dir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null }
$ResultsDir = (Resolve-Path $ResultsDir).Path

Write-Host "=== structbench (n=$N) ===" -ForegroundColor Cyan

function Get-Mean { param([string]$txt)
    foreach ($line in $txt -split "`n") {
        if ($line -match "^\[@bench\]\s+mean\s+(\S+)\s+us") { return [double]$matches[1] }
    }
    return "N/A"
}

$rows = [ordered]@{}

# LS JIT (default)
Write-Host "  LS (JIT) ..." -ForegroundColor Yellow
$out = & $LS run $Dir\structbench.ls $N 2>$null
$rows["LS (JIT)"] = Get-Mean ($out -join "`n")

# LS JIT -O
Write-Host "  LS (JIT -O) ..." -ForegroundColor Yellow
$out = & $LS run -O $Dir\structbench.ls $N 2>$null
$rows["LS (JIT -O)"] = Get-Mean ($out -join "`n")

# LS AOT
Write-Host "  LS (AOT) ..." -ForegroundColor Yellow
& $LS compile $Dir\structbench.ls -o $Dir\structbench_ls.exe 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) {
    $out = & $Dir\structbench_ls.exe $N
    $rows["LS (AOT)"] = Get-Mean ($out -join "`n")
}

# Rust
Write-Host "  Rust (rustc -O) ..." -ForegroundColor Yellow
& rustc -O $Dir\structbench.rs -o $Dir\structbench_rs.exe 2>$null
if ($LASTEXITCODE -eq 0) {
    $out = & $Dir\structbench_rs.exe $N
    $rows["Rust"] = Get-Mean ($out -join "`n")
}

# C++ (MSVC)
Write-Host "  C++ (MSVC /O2) ..." -ForegroundColor Yellow
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vc`" > nul 2>&1 && cl /O2 /std:c++17 /EHsc /MT /nologo `"$Dir\structbench.cpp`" /Fe:`"$Dir\structbench_cpp.exe`" /Fo:`"$Dir\structbench_cpp.obj`" > nul 2>&1"
if ($LASTEXITCODE -eq 0) {
    $out = & $Dir\structbench_cpp.exe $N
    $rows["C++"] = Get-Mean ($out -join "`n")
}

# Python
Write-Host "  Python ..." -ForegroundColor Yellow
# Python is ~300x slower; run 1/50 the iterations and scale the total back up
# so the comparison stays apples-to-apples without a multi-minute wait.
$pyN = [long]($N / 50)
if ($pyN -lt 1) { $pyN = 1 }
$out = & python $Dir\structbench.py $pyN
$pyMean = Get-Mean ($out -join "`n")
if ($pyMean -ne "N/A") { $pyMean = $pyMean * 50 }
$rows["Python (est)"] = $pyMean

# Summary
$fastest = ($rows.Values | Where-Object { $_ -ne "N/A" } | Measure-Object -Minimum).Minimum
$lines = @("structbench results", "===================", "  n = $N (total us for all 3 phases, lower=faster)", "")
foreach ($kv in $rows.GetEnumerator()) {
    if ($kv.Value -ne "N/A") {
        $ratio = "{0,6:N2}x" -f ($kv.Value / $fastest)
        $lines += ("  {0,-12} {1,12:N0} us   {2}" -f $kv.Key, $kv.Value, $ratio)
    }
}
$summary = $lines -join "`n"
$summary | Out-File -FilePath (Join-Path $ResultsDir "structbench_summary.txt") -Encoding utf8
Write-Host "`n$summary" -ForegroundColor Green

Remove-Item "$Dir\*.exe","$Dir\*.obj","$Dir\*.pdb" -Force -ErrorAction SilentlyContinue
