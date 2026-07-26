#requires -version 5
<#
.SYNOPSIS
  Boot a PRE-BUILT Osiris .PRG on the project sim (no compile/link step).
  Mirrors tools/osiris/run-osiris.ps1's deploy+boot exactly, but takes a
  finished .PRG so we can test a binary produced elsewhere.
.EXAMPLE
  pwsh tools/osiris/run-prebuilt-osiris.ps1 -Prg C:\path\HELLO.PRG -Run HELLO -Expect Hello
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Prg,
  [string]$Run = 'HELLO',
  [string[]]$Expect = @('Hello'),
  [int]$BootWait = 5,
  [int]$RunWait = 3,
  [string[]]$SimArgs = @(),  # extra sim flags, e.g. --cpu 68000 --mem MAX
  [switch]$StripRamCache    # blank RAMDRIVE.SYS + SMARTDRV.SYS in config.sys (frees 384K)
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'

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

function _w16($a, $o, $v) { $a[$o] = [byte]($v -band 0xFF); $a[$o + 1] = [byte](($v -shr 8) -band 0xFF) }
function _w32($a, $o, $v) { _w16 $a $o ($v -band 0xFFFF); _w16 $a ($o + 2) (($v -shr 16) -band 0xFFFF) }
function _fat12get($a, $base, $cl) {
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) { (($a[$off] -shr 4) -band 0x0F) -bor (($a[$off + 1] -band 0xFF) -shl 4) }
  else { ($a[$off] -band 0xFF) -bor (($a[$off + 1] -band 0x0F) -shl 8) }
}
function _fat12set($a, $base, $cl, $v) {
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) {
    $a[$off] = [byte](($a[$off] -band 0x0F) -bor (($v -shl 4) -band 0xF0))
    $a[$off + 1] = [byte](($v -shr 4) -band 0xFF)
  } else {
    $a[$off] = [byte]($v -band 0xFF)
    $a[$off + 1] = [byte](($a[$off + 1] -band 0xF0) -bor (($v -shr 8) -band 0x0F))
  }
}
function Add-Fat12File($img, [string]$name11, [byte[]]$data) {
  $FatSz = 9; $RootEnts = 224; $Bpc = 512
  $f1 = 512; $f2 = (1 + $FatSz) * 512
  $rootLba = 1 + 2 * $FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts * 32 / 512.0)
  $dataLba = $rootLba + $rootSecs
  $maxCl = (2880 - $dataLba) + 1
  $ncl = [int][math]::Ceiling($data.Length / [double]$Bpc); if ($ncl -lt 1) { $ncl = 1 }
  $chain = @()
  for ($c = 2; $c -le $maxCl -and $chain.Count -lt $ncl; $c++) {
    if ((_fat12get $img $f1 $c) -eq 0) { $chain += $c }
  }
  if ($chain.Count -lt $ncl) { throw "Add-Fat12File: not enough free clusters ($($chain.Count)/$ncl)" }
  $slot = -1
  for ($i = 0; $i -lt $RootEnts; $i++) {
    $b = $img[$rootLba * 512 + $i * 32]
    if ($b -eq 0x00 -or $b -eq 0xE5) { $slot = $i; break }
  }
  if ($slot -lt 0) { throw "Add-Fat12File: no free root-dir slot" }
  for ($i = 0; $i -lt $ncl; $i++) {
    $c = $chain[$i]
    $dst = ($dataLba + ($c - 2)) * 512
    $n = [math]::Min($Bpc, $data.Length - $i * $Bpc)
    if ($n -gt 0) { [Array]::Copy($data, $i * $Bpc, $img, $dst, $n) }
    $nv = if ($i -lt ($ncl - 1)) { $chain[$i + 1] } else { 0xFFF }
    _fat12set $img $f1 $c $nv; _fat12set $img $f2 $c $nv
  }
  $r = $rootLba * 512 + $slot * 32
  [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img, $r)
  $img[$r + 0x0B] = 0x20
  _w16 $img ($r + 0x1A) $chain[0]; _w32 $img ($r + 0x1C) $data.Length
}
function Remove-Fat12File($img, [string]$name11) {
  $FatSz = 9; $RootEnts = 224
  $f1 = 512; $f2 = (1 + $FatSz) * 512
  $rootLba = 1 + 2 * $FatSz
  for ($i = 0; $i -lt $RootEnts; $i++) {
    $r = $rootLba * 512 + $i * 32
    $b = $img[$r]
    if ($b -eq 0x00 -or $b -eq 0xE5) { continue }
    if ([Text.Encoding]::ASCII.GetString($img, $r, 11) -ne $name11) { continue }
    $cl = ($img[$r + 0x1A] -band 0xFF) -bor (($img[$r + 0x1B] -band 0xFF) -shl 8)
    while ($cl -ge 2 -and $cl -lt 0xFF0) {
      $nx = _fat12get $img $f1 $cl
      _fat12set $img $f1 $cl 0; _fat12set $img $f2 $cl 0
      $cl = $nx
    }
    $img[$r] = 0xE5
  }
}

$sim = Join-Path $simenv 'c68k-sim68k.exe'
$rom = Join-Path $simenv 'bootrom.bin'
$baseImg = Join-Path $simenv 'osiris-boot-144.img'
foreach ($p in @($sim, $rom, $baseImg, $Prg)) {
  if (-not (Test-Path $p)) { throw "run-prebuilt-osiris: missing '$p'" }
}

Stop-AllSim
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-osiris-pre'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$img = Join-Path $work 'os.img'
$log = Join-Path $work 'con.log'
$rtc = Join-Path $work 'rtc.nv'

$bz = [IO.File]::ReadAllBytes($baseImg)
if ($StripRamCache) {
  $txt = [Text.Encoding]::GetEncoding(28591).GetString($bz)
  foreach ($dev in @('DEVICE=RAMDRIVE.SYS', 'DEVICE=SMARTDRV.SYS')) {
    $k = $txt.IndexOf($dev)
    if ($k -ge 0) {
      $e = $k
      while ($e -lt $bz.Length -and $bz[$e] -ne 0x0D -and $bz[$e] -ne 0x0A) { $bz[$e] = 0x20; $e++ }
      Write-Host ("config.sys: blanked '{0}' ({1} bytes)" -f $dev, ($e - $k))
    } else { Write-Host "config.sys: '$dev' not found" }
  }
}
$stem = $Run.ToUpper(); if ($stem.Length -gt 8) { $stem = $stem.Substring(0, 8) } else { $stem = $stem.PadRight(8) }
$n11 = $stem + 'PRG'
Remove-Fat12File $bz $n11
Add-Fat12File $bz $n11 ([IO.File]::ReadAllBytes($Prg))
[IO.File]::WriteAllBytes($img, $bz)
Write-Host ("deployed {0} ({1:N0} bytes) as {2}" -f $Prg, (Get-Item $Prg).Length, $n11)

Remove-Item $log -ErrorAction SilentlyContinue
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc, (New-Object byte[] 64)) }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $sim
foreach ($arg in (@("--rom:$rom", '--fd0', $img, '--acia-port', 'none', '--fdc-threads', 'off', '--rtc-nv', $rtc, '--tee-acia', $log) + $SimArgs)) {
  [void]$psi.ArgumentList.Add($arg)
}
$psi.RedirectStandardInput = $true
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)
function _send($proc, [string]$s) {
  $b = [Text.Encoding]::ASCII.GetBytes($s)
  $proc.StandardInput.BaseStream.Write($b, 0, $b.Length)
  $proc.StandardInput.BaseStream.Flush()
}
try {
  Start-Sleep -Seconds $BootWait
  _send $p ("{0}`r" -f $Run)
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
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
exit $rc
