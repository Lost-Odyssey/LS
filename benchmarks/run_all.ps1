param(
    [int]$N = 35,
    [int]$Iters = 10
)
$ErrorActionPreference = "Stop"
$BenchDir = $PSScriptRoot

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  LS Language Benchmarks — run_all" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 逐个运行各 benchmark 目录
$benchDirs = Get-ChildItem -Path $BenchDir -Directory | Where-Object { Test-Path "$_\run.ps1" } | Sort-Object Name
if ($benchDirs.Count -eq 0) {
    Write-Host "No benchmarks found (no subdirectories with run.ps1)" -ForegroundColor Red
    exit 1
}

foreach ($dir in $benchDirs) {
    Write-Host "`n----------------------------------------" -ForegroundColor Magenta
    & "$dir\run.ps1" -N $N -Iters $Iters -ResultsDir "$BenchDir\results"
}

# 最终汇总
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$summaries = Get-ChildItem -Path "$BenchDir\results" -Filter "*_summary.txt" | Sort-Object Name
foreach ($s in $summaries) {
    Write-Host (Get-Content $s.FullName -Raw)
}
