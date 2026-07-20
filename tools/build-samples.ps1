#requires -version 5
<#
.SYNOPSIS
  Build the standalone samples/ gallery for BOTH targets (Osiris .PRG and
  CP/M-68K .68K) and report per-sample status and code size.

.DESCRIPTION
  A build-coverage gate and an artifact gallery: every single-source sample is
  compiled for both OSes with c68k, so a codegen or libc regression that breaks
  a real program surfaces here even if it is not in the lockstep golden set.
  Multi-file (multi.c/multi_helper.c) is out of scope for this single-source
  driver. Honours C68K_INTEGRATED_AS and C68K_OPT like the build scripts.

.PARAMETER Opt
  Optimization level to build at (0 or 1). Sets C68K_OPT for the child builds.

.EXAMPLE
  pwsh tools/build-samples.ps1 -Opt 1
#>
[CmdletBinding()]
param(
  [ValidateSet('0', '1')][string]$Opt = '0',
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

# Single-source standalone samples (each defines main and links on its own).
$samples = @('hello', 'filerw', 'printftest', 'fp64conv', 'hexdump', 'bare')

$osBuild  = Join-Path $repo 'tools\osiris\build-prg.ps1'
$cpmBuild = Join-Path $repo 'tools\cpm\build-68k.ps1'
$outDir   = Join-Path ([System.IO.Path]::GetTempPath()) "c68k-gallery-O$Opt"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not $env:C68K_INTEGRATED_AS) { $env:C68K_INTEGRATED_AS = '1' }
$env:C68K_OPT = $Opt

Write-Host ("== c68k samples gallery (-O{0}) ==" -f $Opt) -ForegroundColor Cyan
$rows = @()
$fail = 0
foreach ($s in $samples) {
  $src  = Join-Path $repo "samples\$s.c"
  $name = $s.ToUpper(); if ($name.Length -gt 8) { $name = $name.Substring(0, 8) }

  $prg = $null; $k68 = $null
  try {
    $prg = @(& $osBuild  -Src $src -Name $name -Cc $Cc -OutDir $outDir 2>&1 |
             Where-Object { "$_" -like '*.PRG' })[-1]
  } catch { }
  try {
    $k68 = @(& $cpmBuild -Src $src -Name $name -Cc $Cc -OutDir $outDir 2>&1 |
             Where-Object { "$_" -like '*.68K' })[-1]
  } catch { }

  $prgOk = $prg -and (Test-Path $prg)
  $k68Ok = $k68 -and (Test-Path $k68)
  $prgSz = if ($prgOk) { (Get-Item $prg).Length } else { 0 }
  $k68Sz = if ($k68Ok) { (Get-Item $k68).Length } else { 0 }
  if (-not ($prgOk -and $k68Ok)) { $fail++ }

  $rows += [pscustomobject]@{
    Sample     = $s
    'PRG'      = if ($prgOk) { $prgSz } else { 'FAIL' }
    '.68K'     = if ($k68Ok) { $k68Sz } else { 'FAIL' }
  }
}

$rows | Format-Table -AutoSize | Out-String | Write-Host
$env:C68K_OPT = $null
if (-not $KeepArtifacts) { Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue }

$total = $samples.Count
Write-Host ("gallery: {0}/{1} samples built for BOTH OSes" -f ($total - $fail), $total) `
  -ForegroundColor ($(if ($fail -eq 0) { 'Green' } else { 'Red' }))
exit $fail
