param(
    [int]$N = 200000,
    [int]$Iters = 5,   # informational; LS/Rust/C++ hardcode iters=5 internally
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
# NOTE: keep "Continue" (not "Stop"): in PowerShell 5.1 a native exe writing to
# stderr (e.g. the JIT's "[jit] compiling ..." notes) under "Stop" raises a
# terminating NativeCommandError even on exit code 0. Every step below already
# checks $LASTEXITCODE explicitly, so "Stop" is unnecessary.
$ErrorActionPreference = "Continue"

$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$Dir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { $ResultsDir = New-Item -ItemType Directory -Path $ResultsDir -Force | Select-Object -ExpandProperty FullName }
else { $ResultsDir = (Resolve-Path $ResultsDir).Path }

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host "=== alloc benchmark (n=$N) ===" -ForegroundColor Cyan

function Extract-Result {
    param([string]$File)
    $content = Get-Content -LiteralPath $File -Raw -ErrorAction SilentlyContinue
    if (-not $content) { return @{result="N/A"; mean="N/A"} }
    $r = "N/A"; $m = "N/A"
    foreach ($line in $content -split "`n") {
        if ($line -match "^result:\s*(\S+)") { $r = $matches[1] }
        if ($line -match "^\[@bench\]\s+mean\s+(\S+)\s+ns") { $m = $matches[1] }
    }
    return @{result=$r; mean=$m}
}

# ── 1. LS JIT ──
Write-Host "  [1/6] LS (JIT) ..." -ForegroundColor Yellow
$f = Join-Path $ResultsDir "alloc_ls_jit.txt"
Push-Location -LiteralPath $Dir
$out = & $LS run alloc.ls $N 2>$null
$ec = $LASTEXITCODE
Pop-Location
$out | Out-File -FilePath $f -Encoding utf8
$ls_jit_ok = $ec -eq 0
if (-not $ls_jit_ok) { Write-Host "    FAILED (exit=$ec)" -ForegroundColor Red }

# ── 2. LS AOT ──
Write-Host "  [2/6] LS (AOT) ..." -ForegroundColor Yellow
$lsExe = Join-Path $Dir "alloc_ls_aot.exe"
$ls_aot_ok = $true
& $LS compile $Dir\alloc.ls -o $lsExe | Out-Null
if ($LASTEXITCODE -eq 0) {
    $f = Join-Path $ResultsDir "alloc_ls_aot.txt"
    Push-Location -LiteralPath $Dir
    $out = & $lsExe $N
    $ec = $LASTEXITCODE
    Pop-Location
    $out | Out-File -FilePath $f -Encoding utf8
    $ls_aot_ok = $ec -eq 0
    if (-not $ls_aot_ok) { Write-Host "    RUN FAILED (exit=$ec)" -ForegroundColor Red }
} else { Write-Host "    COMPILE FAILED" -ForegroundColor Red; $ls_aot_ok = $false }

# ── 3. Rust ──
Write-Host "  [3/6] Rust (rustc -O) ..." -ForegroundColor Yellow
$rsExe = Join-Path $Dir "alloc_rs.exe"
$rs_ok = $true
& rustc -O $Dir\alloc.rs -o $rsExe 2>$null
if ($LASTEXITCODE -eq 0) {
    $f = Join-Path $ResultsDir "alloc_rs.txt"
    $out = & $rsExe $N
    $ec = $LASTEXITCODE
    $out | Out-File -FilePath $f -Encoding utf8
    $rs_ok = $ec -eq 0
    if (-not $rs_ok) { Write-Host "    RUN FAILED (exit=$ec)" -ForegroundColor Red }
} else { Write-Host "    COMPILE FAILED" -ForegroundColor Red; $rs_ok = $false }

# ── 4. C++ (MSVC) ──
Write-Host "  [4/6] C++ (MSVC /O2) ..." -ForegroundColor Yellow
$cppExe = Join-Path $Dir "alloc_cpp.exe"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" > nul 2>&1 && cl /O2 /std:c++17 /EHsc /MT /nologo `"$Dir\alloc.cpp`" /Fe:`"$cppExe`" /Fo:`"$Dir\alloc_cpp.obj`" > nul 2>&1"
$cpp_ok = $LASTEXITCODE -eq 0
if ($cpp_ok) {
    $f = Join-Path $ResultsDir "alloc_cpp.txt"
    $out = & $cppExe $N
    $ec = $LASTEXITCODE
    $out | Out-File -FilePath $f -Encoding utf8
    $cpp_ok = $ec -eq 0
    if (-not $cpp_ok) { Write-Host "    RUN FAILED (exit=$ec)" -ForegroundColor Red }
} else { Write-Host "    COMPILE FAILED" -ForegroundColor Red }

# ── 5. Python ──
Write-Host "  [5/6] Python ..." -ForegroundColor Yellow
$f = Join-Path $ResultsDir "alloc_py.txt"
$out = & python $Dir\alloc.py $N $Iters
$ec = $LASTEXITCODE
$out | Out-File -FilePath $f -Encoding utf8
$py_ok = $ec -eq 0
if (-not $py_ok) { Write-Host "    FAILED (exit=$ec)" -ForegroundColor Red }

# ── 6. Ruby ──
Write-Host "  [6/6] Ruby ..." -ForegroundColor Yellow
$f = Join-Path $ResultsDir "alloc_rb.txt"
$out = & ruby $Dir\alloc.rb $N $Iters
$ec = $LASTEXITCODE
$out | Out-File -FilePath $f -Encoding utf8
$rb_ok = $ec -eq 0
if (-not $rb_ok) { Write-Host "    FAILED (exit=$ec)" -ForegroundColor Red }

# ── Summary ──
$elapsed = $stopwatch.Elapsed.TotalSeconds
$results = [ordered]@{
    "LS (JIT)" = if ($ls_jit_ok) { Extract-Result (Join-Path $ResultsDir "alloc_ls_jit.txt") } else { @{result="N/A";mean="N/A"} }
    "LS (AOT)" = if ($ls_aot_ok) { Extract-Result (Join-Path $ResultsDir "alloc_ls_aot.txt") } else { @{result="N/A";mean="N/A"} }
    "Rust"     = if ($rs_ok)     { Extract-Result (Join-Path $ResultsDir "alloc_rs.txt") }     else { @{result="N/A";mean="N/A"} }
    "C++"      = if ($cpp_ok)    { Extract-Result (Join-Path $ResultsDir "alloc_cpp.txt") }    else { @{result="N/A";mean="N/A"} }
    "Python"   = if ($py_ok)     { Extract-Result (Join-Path $ResultsDir "alloc_py.txt") }     else { @{result="N/A";mean="N/A"} }
    "Ruby"     = if ($rb_ok)     { Extract-Result (Join-Path $ResultsDir "alloc_rb.txt") }     else { @{result="N/A";mean="N/A"} }
}

# fastest mean for ratio column
$fastest = ($results.Values | Where-Object { $_.mean -ne "N/A" } | ForEach-Object { [double]$_.mean } | Measure-Object -Minimum).Minimum

# correctness: all results should be identical
$checks = $results.Values | Where-Object { $_.result -ne "N/A" } | ForEach-Object { $_.result } | Select-Object -Unique
$correctness = if ($checks.Count -le 1) { "all match ($($checks -join ''))" } else { "MISMATCH: $($checks -join ', ')" }

$lines = @(
    "alloc benchmark results",
    "=======================",
    "  n = $N, iterations = 5 (per-iter mean ns)",
    "  workload: vec(string) growth + map(string,int) word-freq, RAII drop",
    "  correctness: $correctness",
    "  total wall time: $("{0:N1}" -f $elapsed) s",
    ""
)
foreach ($kv in $results.GetEnumerator()) {
    if ($kv.Value.mean -ne "N/A") {
        $ratio = "{0,6:N2}x" -f ([double]$kv.Value.mean / $fastest)
        $lines += ("  {0,-9} {1,16:N1} ns   {2}" -f $kv.Key, [double]$kv.Value.mean, $ratio)
    } else {
        $lines += ("  {0,-9} {1,16}     -" -f $kv.Key, "N/A")
    }
}
$summary = $lines -join "`n"
$summary | Out-File -FilePath (Join-Path $ResultsDir "alloc_summary.txt") -Encoding utf8
Write-Host "`n$summary" -ForegroundColor Green
Write-Host "`nElapsed: $("{0:N1}" -f $elapsed)s" -ForegroundColor Gray
