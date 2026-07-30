#requires -version 5
<#
.SYNOPSIS
  P12 SOA-hunt debug launcher: boot Osiris under `sim68k --gdb` with CC.PRG +
  a preprocessed UNICODE.C and an AUTOEXEC.BAT that runs the on-target compile,
  so a debugger (sid68k or gdb) can catch the two DebugBreak() (ILLEGAL) traps
  bracketing __heap_release() and read the heap handles from D0/D1.

.DESCRIPTION
  sim68k --gdb WAITS for the debugger before running. This script stages the
  disk, starts the sim (detached, listening on the RSP port), and returns so
  you can attach:

    sid68k --sim localhost:<Port>        (then arm an illegal-instruction catch, G)
      -- or --
    m68k-elf-gdb                         (target remote localhost:<Port>)

  On boot the AUTOEXEC.BAT runs `CC -c UNICODE.C -o UNICODE.O`. cc1() compiles
  on the scratch arena; at the first DebugBreak() (before __heap_release) D0 =
  machine-heap handle, D1 = scratch-arena handle; the second DebugBreak (after
  release) shows the same two values with the arena block now freed.

  Layout matches tools/osiris/stage3-cc.ps1 (--cpu 68000 --mem MAX) so the
  (layout-sensitive) corruption reproduces identically.
#>
[CmdletBinding()]
param(
  [int]$Port       = 9001,
  [string]$Sim     = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\sim68k.exe',
  [string]$CcPrg   = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc\CC.PRG'),
  [string]$Cc      = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Cpu     = '68000',
  [string]$Mem     = 'MAX'
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'
$rom    = Join-Path $simenv 'bootrom.bin'
$baseImg= Join-Path $simenv 'osiris-boot-144.img'
$inc    = Join-Path $repo 'include'; $linc = Join-Path $repo 'libc\include'; $src = Join-Path $repo 'src'
$work   = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-dbg-cc'
New-Item -ItemType Directory -Force -Path $work | Out-Null

foreach ($p in @($Sim,$rom,$baseImg,$CcPrg,$Cc)) { if (-not (Test-Path $p)) { throw "missing: $p" } }

# ---- FAT12 add-one-file (proven helpers, from debug-link-fault.ps1) ----
function _w16($a,$o,$v){ $a[$o]=[byte]($v -band 0xFF); $a[$o+1]=[byte](($v -shr 8) -band 0xFF) }
function _w32($a,$o,$v){ _w16 $a $o ($v -band 0xFFFF); _w16 $a ($o+2) (($v -shr 16) -band 0xFFFF) }
function _fat12get($a,$base,$cl){ $off=$base+$cl+($cl -shr 1); if($cl -band 1){ (($a[$off] -shr 4) -band 0x0F) -bor (($a[$off+1] -band 0xFF) -shl 4) } else { ($a[$off] -band 0xFF) -bor (($a[$off+1] -band 0x0F) -shl 8) } }
function _fat12set($a,$base,$cl,$v){ $off=$base+$cl+($cl -shr 1); if($cl -band 1){ $a[$off]=[byte](($a[$off] -band 0x0F) -bor (($v -shl 4) -band 0xF0)); $a[$off+1]=[byte](($v -shr 4) -band 0xFF) } else { $a[$off]=[byte]($v -band 0xFF); $a[$off+1]=[byte](($a[$off+1] -band 0xF0) -bor (($v -shr 8) -band 0x0F)) } }
function Name11([string]$n){ $d=$n.LastIndexOf('.'); $s=$n;$e=''; if($d -ge 0){$s=$n.Substring(0,$d);$e=$n.Substring($d+1)}; if($s.Length -gt 8){$s=$s.Substring(0,8)}; ($s.ToUpper().PadRight(8))+($e.ToUpper().PadRight(3)) }
function Add-Fat12File($img,[string]$name11,[byte[]]$data){
  $FatSz=9;$Bpc=512;$f1=512;$f2=(1+$FatSz)*512;$RootEnts=224;$rootLba=1+2*$FatSz
  $rootSecs=[int][math]::Ceiling($RootEnts*32/512.0);$dataLba=$rootLba+$rootSecs;$maxCl=(2880-$dataLba)+1
  $ncl=[int][math]::Ceiling($data.Length/[double]$Bpc); if($ncl -lt 1){$ncl=1}
  $chain=@(); for($c=2;$c -le $maxCl -and $chain.Count -lt $ncl;$c++){ if((_fat12get $img $f1 $c) -eq 0){$chain+=$c} }
  if($chain.Count -lt $ncl){ throw "no clusters (disk full)" }
  $slot=-1; for($i=0;$i -lt $RootEnts;$i++){ $bb=$img[$rootLba*512+$i*32]; if($bb -eq 0 -or $bb -eq 0xE5){$slot=$i;break} }
  if($slot -lt 0){ throw "no root dir slot" }
  for($i=0;$i -lt $ncl;$i++){ $c=$chain[$i]; $dst=($dataLba+($c-2))*512; $n=[math]::Min($Bpc,$data.Length-$i*$Bpc); if($n -gt 0){[Array]::Copy($data,$i*$Bpc,$img,$dst,$n)}; $nv= if($i -lt ($ncl-1)){$chain[$i+1]}else{0xFFF}; _fat12set $img $f1 $c $nv; _fat12set $img $f2 $c $nv }
  $r=$rootLba*512+$slot*32; [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img,$r); $img[$r+0x0B]=0x20; _w16 $img ($r+0x1A) $chain[0]; _w32 $img ($r+0x1C) $data.Length
}

# ---- 1. preprocess unicode.c to a self-contained UNICODE.C (as stage3 does) ----
Write-Host "preprocessing unicode.c ..." -ForegroundColor Cyan
$ppText = & $Cc -E -DC68K_SELFHOST "-I$inc" "-I$linc" "-I$src" (Join-Path $src 'unicode.c') 2>$null
if ($LASTEXITCODE -ne 0) { throw "preprocess unicode.c failed" }
$ppFile = Join-Path $work 'UNICODE.C'
Set-Content -Path $ppFile -Value $ppText -Encoding ASCII
$ppBytes = [IO.File]::ReadAllBytes($ppFile)

# ---- 2. stage fd0 = boot + CC.PRG + UNICODE.C + AUTOEXEC.BAT ----
Write-Host "staging disk ..." -ForegroundColor Cyan
$img = Join-Path $work 'a.img'; $log = Join-Path $work 'sim.log'; $rtc = Join-Path $work 'rtc.nv'
$bz = [IO.File]::ReadAllBytes($baseImg)
Add-Fat12File $bz (Name11 'CC.PRG')       ([IO.File]::ReadAllBytes($CcPrg))
Add-Fat12File $bz (Name11 'UNICODE.C')     $ppBytes
Add-Fat12File $bz (Name11 'AUTOEXEC.BAT') ([Text.Encoding]::ASCII.GetBytes("CC -c UNICODE.C -o UNICODE.O`r`n"))
[IO.File]::WriteAllBytes($img, $bz)
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc,(New-Object byte[] 64)) }
Remove-Item $log -ErrorAction SilentlyContinue
Get-Process sim68k,sid68k,c68k-sim68k -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

# ---- 3. start sim68k --gdb (detached; it WAITS for the debugger) ----
Write-Host "starting sim68k --gdb $Port ..." -ForegroundColor Cyan
$argList = @('--cpu',$Cpu,'--mem',$Mem,"--rom:$rom",'--fd0',$img,'--acia-port','none','--fdc-threads','off','--rtc-nv',$rtc,'--tee-acia',$log,'--gdb',"$Port")
$p = Start-Process -FilePath $Sim -ArgumentList $argList -PassThru
Start-Sleep -Milliseconds 800

Write-Host "=========================================================" -ForegroundColor Green
Write-Host ("sim68k running (pid {0}), RSP waiting on localhost:{1}" -f $p.Id, $Port) -ForegroundColor Green
Write-Host "disk: $img   console tee: $log" -ForegroundColor Green
Write-Host "Attach a debugger, then let it run (G / continue):" -ForegroundColor Green
Write-Host "  sid68k --sim localhost:$Port" -ForegroundColor Yellow
Write-Host "On boot AUTOEXEC runs: CC -c UNICODE.C -o UNICODE.O" -ForegroundColor Green
Write-Host "At the 1st ILLEGAL (before __heap_release): D0 = machine-heap, D1 = scratch-arena." -ForegroundColor Green
Write-Host "At the 2nd ILLEGAL (after  __heap_release): same handles, arena block now freed." -ForegroundColor Green
Write-Host "=========================================================" -ForegroundColor Green
