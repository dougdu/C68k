#requires -version 5
<#
.SYNOPSIS
  Build a c68k C program to an Osiris .PRG, run it on Osiris under the
  project-local sim, and check its console output.

.DESCRIPTION
  Mirrors the pascal68k hermetic Osiris harness: copies the vendored FAT12
  boot floppy, adds the freshly built .PRG (add-one-file, no external tool),
  boots simenv/c68k-sim68k.exe headless via the boot ROM, types the program
  name at the A> shell, captures the ACIA console with --tee-acia, and asserts
  the expected substrings. Auto-bootstraps simenv/ if needed.

.EXAMPLE
  pwsh tools/osiris/run-osiris.ps1 -Src samples/hello.c -Run HELLO -Expect 'Hello, Osiris'
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Src,     # program .c source
  [string]$Run = '',                      # command typed at the shell (default: source stem, upper)
  [string[]]$Expect = @(),                # substrings that must all appear
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [int]$BootWait = 5,
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

# ---- FAT12 add-one-file helpers (1.44 MB geometry; from Osiris mkboot) ----
function _w16($a,$o,$v){ $a[$o]=[byte]($v -band 0xFF); $a[$o+1]=[byte](($v -shr 8) -band 0xFF) }
function _w32($a,$o,$v){ _w16 $a $o ($v -band 0xFFFF); _w16 $a ($o+2) (($v -shr 16) -band 0xFFFF) }
function _fat12get($a,$base,$cl){
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) { (($a[$off] -shr 4) -band 0x0F) -bor (($a[$off+1] -band 0xFF) -shl 4) }
  else             { ($a[$off] -band 0xFF) -bor (($a[$off+1] -band 0x0F) -shl 8) }
}
function _fat12set($a,$base,$cl,$v){
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) {
    $a[$off]   = [byte](($a[$off] -band 0x0F) -bor (($v -shl 4) -band 0xF0))
    $a[$off+1] = [byte](($v -shr 4) -band 0xFF)
  } else {
    $a[$off]   = [byte]($v -band 0xFF)
    $a[$off+1] = [byte](($a[$off+1] -band 0xF0) -bor (($v -shr 8) -band 0x0F))
  }
}
function Add-Fat12File($img, [string]$name11, [byte[]]$data){
  $FatSz=9; $RootEnts=224; $Bpc=512
  $f1 = 512; $f2 = (1+$FatSz)*512
  $rootLba = 1 + 2*$FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts*32/512.0)
  $dataLba = $rootLba + $rootSecs
  $maxCl = (2880 - $dataLba) + 1
  $ncl = [int][math]::Ceiling($data.Length / [double]$Bpc); if ($ncl -lt 1){ $ncl=1 }
  $chain = @()
  for ($c=2; $c -le $maxCl -and $chain.Count -lt $ncl; $c++){
    if ((_fat12get $img $f1 $c) -eq 0) { $chain += $c }
  }
  if ($chain.Count -lt $ncl) { throw "Add-Fat12File: not enough free clusters ($($chain.Count)/$ncl)" }
  $slot = -1
  for ($i=0; $i -lt $RootEnts; $i++){
    $b = $img[$rootLba*512 + $i*32]
    if ($b -eq 0x00 -or $b -eq 0xE5) { $slot = $i; break }
  }
  if ($slot -lt 0) { throw "Add-Fat12File: no free root-dir slot" }
  for ($i=0; $i -lt $ncl; $i++){
    $c = $chain[$i]
    $dst = ($dataLba + ($c-2))*512
    $n = [math]::Min($Bpc, $data.Length - $i*$Bpc)
    if ($n -gt 0) { [Array]::Copy($data, $i*$Bpc, $img, $dst, $n) }
    $nv = if ($i -lt ($ncl-1)) { $chain[$i+1] } else { 0xFFF }
    _fat12set $img $f1 $c $nv; _fat12set $img $f2 $c $nv
  }
  $r = $rootLba*512 + $slot*32
  [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img, $r)
  $img[$r+0x0B] = 0x20
  _w16 $img ($r+0x1A) $chain[0]; _w32 $img ($r+0x1C) $data.Length
}
function Remove-Fat12File($img, [string]$name11){
  $FatSz=9; $RootEnts=224
  $f1 = 512; $f2 = (1+$FatSz)*512
  $rootLba = 1 + 2*$FatSz
  for ($i=0; $i -lt $RootEnts; $i++){
    $r = $rootLba*512 + $i*32
    $b = $img[$r]
    if ($b -eq 0x00 -or $b -eq 0xE5) { continue }
    if ([Text.Encoding]::ASCII.GetString($img, $r, 11) -ne $name11) { continue }
    $cl = ($img[$r+0x1A] -band 0xFF) -bor (($img[$r+0x1B] -band 0xFF) -shl 8)
    while ($cl -ge 2 -and $cl -lt 0xFF0) {
      $nx = _fat12get $img $f1 $cl
      _fat12set $img $f1 $cl 0; _fat12set $img $f2 $cl 0
      $cl = $nx
    }
    $img[$r] = 0xE5
  }
}

# ---- ensure simenv ----
if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) {
  & (Join-Path $repo 'tools\bootstrap-simenv.ps1')
}
$sim     = Join-Path $simenv 'c68k-sim68k.exe'
$rom     = Join-Path $simenv 'bootrom.bin'
$baseImg = Join-Path $simenv 'osiris-boot-144.img'
foreach ($p in @($sim,$rom,$baseImg)) {
  if (-not (Test-Path $p)) { throw "run-osiris: missing simenv asset '$p'. Run tools/bootstrap-simenv.ps1." }
}

# ---- build the .PRG ----
$prg = @(& (Join-Path $PSScriptRoot 'build-prg.ps1') -Src $Src -Name $Run -Cc $Cc |
         Where-Object { "$_" -like '*.PRG' })[-1]
if (-not $prg -or -not (Test-Path $prg)) { throw "run-osiris: build produced no .PRG" }

# ---- scratch floppy with the .PRG added ----
Stop-AllSim
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-osiris-run'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$img = Join-Path $work 'os.img'
$log = Join-Path $work 'con.log'
$rtc = Join-Path $work 'rtc.nv'

$bz = [IO.File]::ReadAllBytes($baseImg)
$stem = $Run.ToUpper(); if ($stem.Length -gt 8) { $stem = $stem.Substring(0,8) } else { $stem = $stem.PadRight(8) }
$n11 = $stem + 'PRG'
Remove-Fat12File $bz $n11
Add-Fat12File $bz $n11 ([IO.File]::ReadAllBytes($prg))
[IO.File]::WriteAllBytes($img, $bz)

Remove-Item $log -ErrorAction SilentlyContinue
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc, (New-Object byte[] 64)) }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $sim
foreach ($arg in @("--rom:$rom",'--fd0',$img,'--acia-port','none','--fdc-threads','off','--rtc-nv',$rtc,'--tee-acia',$log)) {
  [void]$psi.ArgumentList.Add($arg)
}
$psi.RedirectStandardInput = $true
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)

function _send($proc,[string]$s){
  $b = [Text.Encoding]::ASCII.GetBytes($s)
  $proc.StandardInput.BaseStream.Write($b,0,$b.Length)
  $proc.StandardInput.BaseStream.Flush()
}

try {
  Start-Sleep -Seconds $BootWait          # boot ROM -> Osiris -> A>
  _send $p ("{0}`r" -f $Run)              # launch the program
  Start-Sleep -Seconds $RunWait
} finally {
  try { $p.Kill() } catch {}
  $p.WaitForExit()
  Stop-AllSim
}

$logText = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
Write-Host "===== Osiris ACIA console ====="
Write-Host $logText
Write-Host "==============================="

$rc = 0
foreach ($e in $Expect) {
  $pat = [Management.Automation.WildcardPattern]::Escape($e)
  if ($logText -like "*$pat*") { Write-Host "OSIRIS: found '$e'" -ForegroundColor Green }
  else { Write-Host "OSIRIS: MISSING '$e'" -ForegroundColor Red; $rc = 1 }
}
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if ($Expect.Count -gt 0 -and $rc -eq 0) { Write-Host "OSIRIS: PASS" -ForegroundColor Green }
exit $rc
