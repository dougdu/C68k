#requires -version 5
<#
.SYNOPSIS
  Build a c68k C program to a CP/M-68K .68K, run it on CP/M-68K under the
  project-local sim, and check its console output.

.DESCRIPTION
  Mirrors the pascal68k hermetic CP/M harness: boots CP/M-68K from the vendored
  floppy via the Osiris boot ROM (so a locked/absent hard disk can't block
  boot), deploys the freshly built .68K onto a scratch copy of scsi1 (drive D:)
  with cpmcp, boots simenv/c68k-sim68k.exe headless, selects D:, types the
  program name, captures the ACIA console, and asserts the expected substrings.

  Drive map: A: = CP/M boot floppy (utilities), B: = unmounted (DO NOT touch),
  C: = scsi0 (system), D: = scsi1 (deploy target).

  -Model selects the CBIOS memory platform: '16mb' (cpu=68000, default) boots
  cpmboot-16mb-144.img; '1mb' (cpu=68008) boots cpmboot-1mb-144.img.

.EXAMPLE
  pwsh tools/cpm/run-cpm.ps1 -Src samples/hello.c -Expect 'Hello, Osiris'
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Src,
  [string]$Run = '',
  [string[]]$Expect = @(),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [ValidateSet('16mb','1mb')][string]$Model = '16mb',
  [int]$BootWait = 8,
  [int]$RunWait = 3,
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'
if (-not $Run) { $Run = [IO.Path]::GetFileNameWithoutExtension($Src).ToUpper() }

function Stop-AllSim {
  param([int]$SettleMs = 900, [int]$TimeoutMs = 6000)
  $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
  while ((Get-Date) -lt $deadline) {
    $ps = Get-Process c68k-sim68k -ErrorAction SilentlyContinue
    if (-not $ps) { break }
    $ps | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
  }
  Start-Sleep -Milliseconds $SettleMs
}

if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) {
  & (Join-Path $repo 'tools\bootstrap-simenv.ps1')
}
$sim   = Join-Path $simenv 'c68k-sim68k.exe'
$rom   = Join-Path $simenv 'bootrom.bin'
$cpu   = if ($Model -eq '1mb') { '68008' } else { '68000' }
$flop  = Join-Path $simenv "cpmboot-$Model-144.img"
$scsi0 = Join-Path $simenv 'scsi0.img'
$scsi1 = Join-Path $simenv 'scsi1.img'
$cpmrm = Join-Path $simenv 'cpmrm.exe'
$cpmcp = Join-Path $simenv 'cpmcp.exe'
$fmt   = 'worm68k-8m'
foreach ($p in @($sim,$rom,$flop,$scsi0,$scsi1,$cpmrm,$cpmcp)) {
  if (-not (Test-Path $p)) { throw "run-cpm: missing simenv asset '$p'. Run tools/bootstrap-simenv.ps1." }
}

# ---- build the .68K ----
$k = @(& (Join-Path $PSScriptRoot 'build-68k.ps1') -Src $Src -Name $Run -Cc $Cc |
       Where-Object { "$_" -like '*.68K' })[-1]
if (-not $k -or -not (Test-Path $k)) { throw "run-cpm: build produced no .68K" }

Stop-AllSim
$work    = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cpm-run'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$bootImg = Join-Path $work 'cpmboot.img'
$dataImg = Join-Path $work 'scsi1.img'
$outF    = Join-Path $work 'con.log'
Copy-Item $flop  $bootImg -Force
Copy-Item $scsi1 $dataImg -Force
Remove-Item $outF -ErrorAction SilentlyContinue

# deploy onto D: (scsi1) as NAME.68K, user 0
$dn = "0:$($Run.ToUpper()).68K"
& $cpmrm -f $fmt $dataImg $dn 2>&1 | Out-Null
& $cpmcp -f $fmt $dataImg $k $dn 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cpmcp failed to deploy $dn (rc=$LASTEXITCODE)" }

$a = @(
  '--cpu',$cpu,'--mem','MAX',"--rom:$rom",
  '--acia-port','none','--acia-cts','tied','--tee-acia',$outF,'--fdc-threads','on',
  '--fd0',$bootImg,'--scsi0',$scsi0,'--scsi1',$dataImg
)
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $sim
foreach ($arg in $a) { [void]$psi.ArgumentList.Add($arg) }
$psi.WorkingDirectory = $simenv
$psi.RedirectStandardInput = $true
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)

function _send($proc,[string]$s){
  $b = [Text.Encoding]::ASCII.GetBytes($s)
  $proc.StandardInput.BaseStream.Write($b,0,$b.Length)
  $proc.StandardInput.BaseStream.Flush()
}

try {
  Start-Sleep -Seconds $BootWait     # ROM boot -> CP/M CCP 'A>'
  _send $p "D:`r"                     # select drive D:
  Start-Sleep -Milliseconds 800
  _send $p ("{0}`r" -f $Run)          # run the program
  Start-Sleep -Seconds $RunWait
} finally {
  try { $p.Kill() } catch {}
  try { $p.WaitForExit() } catch {}
  Stop-AllSim
}

$logText = if (Test-Path $outF) { (Get-Content -Raw $outF) -replace "`r","" } else { '' }
Write-Host "===== CP/M-68K console ====="
Write-Host $logText
Write-Host "============================"

$rc = 0
foreach ($e in $Expect) {
  $pat = [Management.Automation.WildcardPattern]::Escape($e)
  if ($logText -like "*$pat*") { Write-Host "CPM: found '$e'" -ForegroundColor Green }
  else { Write-Host "CPM: MISSING '$e'" -ForegroundColor Red; $rc = 1 }
}
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if ($Expect.Count -gt 0 -and $rc -eq 0) { Write-Host "CPM: PASS" -ForegroundColor Green }
exit $rc
