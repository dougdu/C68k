#requires -version 5
<#
.SYNOPSIS
  P9 native-link harness: link c68k objects with the native Osiris LINK.PRG
  (running on Osiris under sim68k), then run the resulting .PRG and check it.

.DESCRIPTION
  Proves the native Osiris toolchain chain consumes c68k's own objects:

      c68k -fintegrated-as -c foo.c   ->  FOO.O   (integrated ELF emitter, P8)
      LINK -o FOO.PRG SYS.O FOO.O LIBC.O RT68K.O FLOAT.A   (on Osiris)
      FOO                                                   (run it)

  crt0/seam (osiris_sys.a68) and the integer runtime (rt68k.a68) are still
  assembled with asm68K -- they use asm68K-only directives -- but the C code
  (libc + the program) is emitted DIRECTLY by c68k with no assembler. The
  linker is the real native LINK.PRG from the Osiris toolchain.

  Everything is staged on one FAT12 boot floppy; the shell is driven over the
  ACIA: type the LINK command, wait, type the program name, capture output.

.EXAMPLE
  pwsh tools/osiris/run-native-link.ps1 -Src samples/hello.c -Run HELLO -Expect 'Hello, Osiris'
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Src,
  [string]$Run = '',
  [string[]]$Extra = @(),   # additional .c TUs to compile + link (multi-object demo)
  [string[]]$Expect = @(),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Asm = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$LinkPrg = 'C:\git\osiris\build\LINK.PRG',
  [string]$LibPrg = 'C:\git\osiris\build\LIB.PRG',
  [string]$FloatLib = 'C:\git\worm68k\68kTools\libraries\float\ieee754\libieee754d.a',
  [switch]$UseLib,          # archive libc.o into LIBC.A with LIB.PRG, link that
  [switch]$NoIntegrated,    # compile C via asm68K (isolate integrated-emitter issues)
  [switch]$Bare,            # link crt0/seam + program + runtime only (no libc/float)
  [switch]$NoFloat,         # link libc.o but not the float archive (isolate FLOAT.A)
  [string]$Cpu = '',        # sim68k --cpu (e.g. 68000 for the full 24-bit/16MB model)
  [string]$Mem = '',        # sim68k --mem (e.g. MAX)
  [int]$BootWait = 5,
  [int]$LinkWait = 40,
  [int]$RunWait = 3,
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'
if (-not $Run) { $Run = [IO.Path]::GetFileNameWithoutExtension($Src).ToUpper() }
$Src = (Resolve-Path $Src).Path

# ---- FAT12 helpers (verbatim from run-osiris.ps1: proven add-one-file) ----
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
function Name11([string]$name){
  $dot=$name.LastIndexOf('.'); $stem=$name; $ext=''
  if ($dot -ge 0){ $stem=$name.Substring(0,$dot); $ext=$name.Substring($dot+1) }
  if ($stem.Length -gt 8){ $stem=$stem.Substring(0,8) }
  ($stem.ToUpper().PadRight(8)) + ($ext.ToUpper().PadRight(3))
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

function Stop-AllSim {
  $deadline=(Get-Date).AddMilliseconds(6000)
  while((Get-Date) -lt $deadline){ $ps=Get-Process c68k-sim68k -ErrorAction SilentlyContinue; if(-not $ps){break}; $ps|Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 200 }
  Start-Sleep -Milliseconds 600
}

# ---- ensure simenv ----
if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) { & (Join-Path $repo 'tools\bootstrap-simenv.ps1') }
$sim=Join-Path $simenv 'c68k-sim68k.exe'; $rom=Join-Path $simenv 'bootrom.bin'; $baseImg=Join-Path $simenv 'osiris-boot-144.img'
foreach($p in @($sim,$rom,$baseImg,$LinkPrg,$FloatLib)){ if(-not (Test-Path $p)){ throw "run-native-link: missing '$p'" } }

# ---- build the objects (crt0/rt via asm68K; C via c68k integrated emitter) ----
$work=Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-native-link'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$inc=Join-Path $repo 'libc\include'
$sysO=Join-Path $work 'SYS.O'; $rtO=Join-Path $work 'RT68K.O'; $libcO=Join-Path $work 'LIBC.O'; $progO=Join-Path $work "$Run.O"
$sysA=Join-Path $repo 'libc\osiris\osiris_sys.a68'; $rtA=Join-Path $repo 'lib\runtime\rt68k.a68'; $libcC=Join-Path $repo 'libc\core\libc.c'
$ccArgs=@(); if (-not $NoIntegrated) { $ccArgs += '-fintegrated-as' }
function Chk($desc){ if($LASTEXITCODE -ne 0){ throw "$desc failed (rc=$LASTEXITCODE)" } }
& $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA  2>&1 | Out-Null; Chk 'asm crt0'
& $Asm /Cx /elf /c /nologo "/Fo$rtO"  $rtA   2>&1 | Out-Null; Chk 'asm runtime'
if (-not $Bare) { & $Cc @ccArgs -c $libcC -o $libcO "-I$inc" 2>&1 | Out-Null; Chk 'cc libc' }
& $Cc @ccArgs -c $Src   -o $progO "-I$inc"    2>&1 | Out-Null; Chk 'cc program'

# extra translation units (multi-object / archive demo)
$extraObjs=@(); $ei=0
foreach($ex in $Extra){
  $exO=Join-Path $work ("EX{0}.O" -f $ei)
  & $Cc @ccArgs -c (Resolve-Path $ex).Path -o $exO "-I$inc" 2>&1 | Out-Null; Chk "cc extra $ex"
  $extraObjs += @{ N=("EX{0}.O" -f $ei); F=$exO }; $ei++
}

# ---- stage floppy ----
Stop-AllSim
$img=Join-Path $work 'os.img'; $log=Join-Path $work 'con.log'; $rtc=Join-Path $work 'rtc.nv'
$bz=[IO.File]::ReadAllBytes($baseImg)
$stage=@(
  @{ N='LINK.PRG'; F=$LinkPrg },
  @{ N='SYS.O';    F=$sysO },
  @{ N='RT68K.O';  F=$rtO },
  @{ N="$Run.O";   F=$progO }
)
foreach($eo in $extraObjs){ $stage += $eo }
if (-not $Bare) {
  $stage += @{ N='LIBC.O';   F=$libcO }
  if (-not $NoFloat) { $stage += @{ N='FLOAT.A';  F=$FloatLib } }
}
if ($UseLib) { $stage += @{ N='LIB.PRG'; F=$LibPrg } }
foreach($s in $stage){ $n=Name11 $s.N; Remove-Fat12File $bz $n; Add-Fat12File $bz $n ([IO.File]::ReadAllBytes($s.F)) }
[IO.File]::WriteAllBytes($img,$bz)
Remove-Item $log -ErrorAction SilentlyContinue
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc,(New-Object byte[] 64)) }

# ---- boot + drive the shell ----
$psi=New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName=$sim
$simArgs=@()
if ($Cpu) { $simArgs += @('--cpu',$Cpu) }
if ($Mem) { $simArgs += @('--mem',$Mem) }
$simArgs += @("--rom:$rom",'--fd0',$img,'--acia-port','none','--fdc-threads','off','--rtc-nv',$rtc,'--tee-acia',$log)
foreach($a in $simArgs){ [void]$psi.ArgumentList.Add($a) }
$psi.RedirectStandardInput=$true; $psi.UseShellExecute=$false
$p=[System.Diagnostics.Process]::Start($psi)
function _send($proc,[string]$s){ $b=[Text.Encoding]::ASCII.GetBytes($s); $proc.StandardInput.BaseStream.Write($b,0,$b.Length); $proc.StandardInput.BaseStream.Flush() }

# object list for LINK: crt0, program, extras (or their archive), runtime, [libc, float]
$linkObjs = @('SYS.O', "$Run.O")
$useArchive = ($UseLib -and $extraObjs.Count -gt 0)
if ($useArchive) { $linkObjs += 'EXTRA.A' }
else { foreach($eo in $extraObjs){ $linkObjs += $eo.N } }
$linkObjs += 'RT68K.O'
if (-not $Bare) { $linkObjs += 'LIBC.O'; if (-not $NoFloat) { $linkObjs += 'FLOAT.A' } }
$linkCmd = "LINK -o $Run.PRG " + ($linkObjs -join ' ')
try {
  Start-Sleep -Seconds $BootWait
  if ($useArchive) {
    $libCmd = 'LIB rcs EXTRA.A ' + (($extraObjs | ForEach-Object { $_.N }) -join ' ')
    _send $p ("{0}`r" -f $libCmd); Start-Sleep -Seconds 5
    Write-Host "LIB cmd: $libCmd" -ForegroundColor DarkCyan
  }
  _send $p ("{0}`r" -f $linkCmd)
  Start-Sleep -Seconds $LinkWait
  _send $p ("{0}`r" -f $Run)
  Start-Sleep -Seconds $RunWait
} finally {
  try { $p.Kill() } catch {}
  $p.WaitForExit(); Stop-AllSim
}

$logText = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
Write-Host "===== Osiris native-link console ====="
Write-Host $logText
Write-Host "======================================"
Write-Host "LINK cmd: $linkCmd" -ForegroundColor DarkCyan

$rc=0
foreach($e in $Expect){
  $pat=[Management.Automation.WildcardPattern]::Escape($e)
  if ($logText -like "*$pat*"){ Write-Host "NATIVE: found '$e'" -ForegroundColor Green }
  else { Write-Host "NATIVE: MISSING '$e'" -ForegroundColor Red; $rc=1 }
}
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if ($Expect.Count -gt 0 -and $rc -eq 0){ Write-Host "NATIVE: PASS" -ForegroundColor Green }
exit $rc
