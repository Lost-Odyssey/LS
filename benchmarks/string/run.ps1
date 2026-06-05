param(
    [int]$N = 200000,
    [int]$Iters = 10,
    [string]$ResultsDir = "$PSScriptRoot\..\results"
)
$ErrorActionPreference = "Stop"

$LS = "C:\YANG\10003_language\LS\build\Release\ls.exe"
$StrDir = $PSScriptRoot
if (-not (Test-Path $ResultsDir)) { $ResultsDir = New-Item -ItemType Directory -Path $ResultsDir -Force | Select-Object -ExpandProperty FullName }
else { $ResultsDir = (Resolve-Path $ResultsDir).Path }

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host "=== string benchmark (n=$N, iters=$Iters) ===" -ForegroundColor Cyan

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
$f = Join-Path $ResultsDir "string_ls_jit.txt"
Write-Host "  [1/5] LS (JIT) ..." -ForegroundColor Yellow
Push-Location -LiteralPath $StrDir
$ls_jit_out = & $LS run string.ls $N
$ls_jit_ec = $LASTEXITCODE
Pop-Location
$ls_jit_out | Out-File -FilePath $f -Encoding utf8
$ls_jit_ok = $ls_jit_ec -eq 0
if (-not $ls_jit_ok) { Write-Host "    FAILED (exit=$ls_jit_ec)" -ForegroundColor Red }

# ── 2. LS AOT ──
Write-Host "  [2/5] LS (AOT) ..." -ForegroundColor Yellow
$lsExe = Join-Path $StrDir "string_ls_aot.exe"
$ls_aot_ok = $true
& $LS compile $StrDir\string.ls -o $lsExe | Out-Null
if ($LASTEXITCODE -eq 0) {
    $f = Join-Path $ResultsDir "string_ls_aot.txt"
    Push-Location -LiteralPath $StrDir
    $ls_aot_out = & .\string_ls_aot.exe $N
    $ls_aot_ec = $LASTEXITCODE
    Pop-Location
    $ls_aot_out | Out-File -FilePath $f -Encoding utf8
    $ls_aot_ok = $ls_aot_ec -eq 0
    if (-not $ls_aot_ok) { Write-Host "    RUN FAILED (exit=$ls_aot_ec)" -ForegroundColor Red }
} else { Write-Host "    COMPILE FAILED" -ForegroundColor Red; $ls_aot_ok = $false }

# ── 3. C (MSVC) ──
Write-Host "  [3/5] C (MSVC) ..." -ForegroundColor Yellow
$cExe = Join-Path $StrDir "string_c.exe"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" > nul 2>&1 && cl /O2 /MT /nologo `"$StrDir\string.c`" /Fe:`"$cExe`" /Fo:`"$StrDir\string_c.obj`" > nul 2>&1"
$c_ok = $LASTEXITCODE -eq 0
if ($c_ok) {
    $f = Join-Path $ResultsDir "string_c.txt"
    $c_out = & $cExe $N
    $c_ec = $LASTEXITCODE
    $c_out | Out-File -FilePath $f -Encoding utf8
    $c_ok = $c_ec -eq 0
    if (-not $c_ok) { Write-Host "    RUN FAILED (exit=$c_ec)" -ForegroundColor Red }
} else { Write-Host "    COMPILE FAILED" -ForegroundColor Red }

# ── 4. Python (1 iter, mean ×10 in summary) ──
$f = Join-Path $ResultsDir "string_py.txt"
Write-Host "  [4/5] Python (1 iter) ..." -ForegroundColor Yellow
$py_out = python $StrDir\string.py $N 1
$py_ec = $LASTEXITCODE
$py_out | Out-File -FilePath $f -Encoding utf8
$py_ok = $py_ec -eq 0
if (-not $py_ok) { Write-Host "    FAILED (exit=$py_ec)" -ForegroundColor Red }

# ── 5. Ruby (1 iter, mean ×10 in summary) ──
$f = Join-Path $ResultsDir "string_rb.txt"
Write-Host "  [5/5] Ruby (1 iter) ..." -ForegroundColor Yellow
$rb_out = ruby $StrDir\string.rb $N 1
$rb_ec = $LASTEXITCODE
$rb_out | Out-File -FilePath $f -Encoding utf8
$rb_ok = $rb_ec -eq 0
if (-not $rb_ok) { Write-Host "    FAILED (exit=$rb_ec)" -ForegroundColor Red }

# ── Summary ──
$elapsed = $stopwatch.Elapsed.TotalSeconds
function Multiply-Mean10 {
    param($r)
    if ($r.mean -ne "N/A") { $r.mean = [double]$r.mean * 10.0 }
    return $r
}

$results = @{
    "LS (JIT)" = if ($ls_jit_ok) { Extract-Result (Join-Path $ResultsDir "string_ls_jit.txt") } else { @{r="N/A";m="N/A"} }
    "LS (AOT)" = if ($ls_aot_ok) { Extract-Result (Join-Path $ResultsDir "string_ls_aot.txt") } else { @{r="N/A";m="N/A"} }
    "C (MSVC)" = if ($c_ok)      { Extract-Result (Join-Path $ResultsDir "string_c.txt") }       else { @{r="N/A";m="N/A"} }
    "Python"   = if ($py_ok)     { Multiply-Mean10 (Extract-Result (Join-Path $ResultsDir "string_py.txt")) } else { @{r="N/A";m="N/A"} }
    "Ruby"     = if ($rb_ok)     { Multiply-Mean10 (Extract-Result (Join-Path $ResultsDir "string_rb.txt")) } else { @{r="N/A";m="N/A"} }
}

$lines = @(
    "string benchmark results",
    "=========================",
    "  n = $N, iterations = $Iters",
    "  (Python/Ruby: 1 actual iter, mean ×10 for 10-iters estimate)",
    "  total wall time: $("{0:N1}" -f $elapsed) s",
    ""
)
foreach ($kv in $results.GetEnumerator() | Sort-Object Name) {
    $lines += "$($kv.Name): $($kv.Value.mean) ns  (result=$($kv.Value.result))"
}
$summary = $lines -join "`n"
$summary | Out-File -FilePath (Join-Path $ResultsDir "string_summary.txt") -Encoding utf8
Write-Host "`n$summary" -ForegroundColor Green
Write-Host "`nElapsed: $("{0:N1}" -f $elapsed)s" -ForegroundColor Gray
