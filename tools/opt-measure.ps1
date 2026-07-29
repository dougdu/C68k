#requires -version 5
<#
.SYNOPSIS
  Optimizer benchmark: build the tests/opt corpus at -O0..-O3 and record the
  emitted instruction count and .text size per (file, level). The OP0 baseline
  and every later phase's size/speed delta (docs/optimization-plan.md) come from
  here.

.DESCRIPTION
  For each corpus .c and each -O level:
    * `c68k -S -O<n>` -> count instruction lines (2-space-indented mnemonics),
    * `c68k -c -fintegrated-as -O<n>` -> a relocatable .o, and m68k-elf-size ->
      the .text byte count (the "real" code size; skipped if size isn't found).
  Prints a per-file table and the -O1/-O2/-O3 delta vs the -O0 baseline. With
  -Csv it also writes a machine-readable baseline for regression tracking.

.EXAMPLE
  pwsh tools/opt-measure.ps1
  pwsh tools/opt-measure.ps1 -Csv out\opt-baseline.csv
#>
[CmdletBinding()]
param(
  [string]$Cc     = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\c68k.exe'),
  [string[]]$Corpus = @(),
  [string]$Size   = 'C:\git\osiris\toolchain\binutils\m68k-elf-size.exe',
  [string]$Csv    = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path $Cc)) { throw "opt-measure: compiler not found: $Cc (build it: cmake --build build --config Release)" }
if (-not $Corpus) { $Corpus = @(Get-ChildItem (Join-Path $repo 'tests\opt') -Filter *.c | ForEach-Object { $_.FullName }) }
$haveSize = Test-Path $Size

$work = Join-Path ([IO.Path]::GetTempPath()) 'c68k-optmeasure'
New-Item -ItemType Directory -Force -Path $work | Out-Null

# instruction lines = 2-space-indented lowercase mnemonics (labels are at col 0;
# directives are '.'-prefixed or UPPER-case; there are no comments without -g).
function Get-InsnCount([string]$s) {
  (Get-Content $s | Where-Object { $_ -match '^  [a-z]' }).Count
}
function Get-TextBytes([string]$o) {
  if (-not $haveSize) { return -1 }
  $out = & $Size $o 2>$null
  foreach ($ln in $out) { if ($ln -match '^\s*(\d+)\s+\d+\s+\d+\s+\d+') { return [int]$matches[1] } }
  return -1
}

$rows = @()
foreach ($src in $Corpus) {
  $name = [IO.Path]::GetFileName($src)
  foreach ($o in 0,1,2,3) {
    $asm = Join-Path $work ("{0}.O{1}.s" -f $name, $o)
    $obj = Join-Path $work ("{0}.O{1}.o" -f $name, $o)
    & $Cc -S "-O$o" $src -o $asm 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "opt-measure: cc -S failed on $name -O$o" }
    & $Cc -c -fintegrated-as "-O$o" $src -o $obj 2>&1 | Out-Null
    $rows += [pscustomobject]@{ File=$name; O=$o; Insns=(Get-InsnCount $asm); Text=(Get-TextBytes $obj) }
  }
}

Write-Host ''
Write-Host 'c68k optimizer benchmark  (instruction count / .text bytes)' -ForegroundColor Cyan
Write-Host ('{0,-14} {1,5} {2,8} {3,10} {4,8}' -f 'file', 'lvl', 'insns', '.text', 'vs O0')
Write-Host ('-' * 48)
$totalO0 = 0; $totalO1 = 0
foreach ($f in ($rows | Group-Object File)) {
  $o0 = ($f.Group | Where-Object O -eq 0)
  foreach ($r in ($f.Group | Sort-Object O)) {
    $delta = if ($r.O -eq 0) { '' } else { '{0:+0;-0;0}' -f ($r.Insns - $o0.Insns) }
    $txt = if ($r.Text -ge 0) { $r.Text } else { 'n/a' }
    Write-Host ('{0,-14} {1,5} {2,8} {3,10} {4,8}' -f $r.File, "-O$($r.O)", $r.Insns, $txt, $delta)
  }
  $totalO0 += $o0.Insns
  $totalO1 += ($f.Group | Where-Object O -eq 1).Insns
  Write-Host ('-' * 48)
}
$pct = if ($totalO0) { [math]::Round(100.0 * ($totalO0 - $totalO1) / $totalO0, 1) } else { 0 }
Write-Host ("corpus total insns: -O0 {0} -> -O1 {1}  ({2}% fewer)" -f $totalO0, $totalO1, $pct) -ForegroundColor Green
if (-not $haveSize) { Write-Host "(.text = n/a: m68k-elf-size not found at $Size)" -ForegroundColor DarkGray }

if ($Csv) {
  $rows | Export-Csv -Path $Csv -NoTypeInformation
  Write-Host "baseline written: $Csv" -ForegroundColor DarkCyan
}
