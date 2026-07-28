#requires -version 5
<#
.SYNOPSIS
  P9 native-link harness: link c68k objects with the native Osiris LINK.PRG
  (running on Osiris under sim68k), then run the resulting .PRG and check it.

.DESCRIPTION
  Proves the native Osiris toolchain chain consumes c68k's own objects:

      c68k -fintegrated-as -c foo.c   ->  FOO.O   (integrated ELF emitter, P8)
      LINK -o FOO.PRG SYS.O FOO.O RT68K.O LIBC68K.A  (on Osiris)
      FOO                                                   (run it)

  crt0/seam (osiris_sys.a68) and the integer runtime (rt68k.a68) are still
  assembled with asm68K -- they use asm68K-only directives -- but the C code
  (libc + the program) is emitted DIRECTLY by c68k with no assembler. The
  linker is the real native LINK.PRG from the Osiris toolchain.

  A: is the pristine boot floppy; the toolchain + inputs are staged on a fresh
  B: data floppy (fd1, ~1.42 MB free -- the grown archives no longer fit beside
  the OS on the 1.44 MB boot floppy). The shell is driven over the ACIA: switch
  to B:, type the LINK command, wait, type the program name, capture output.

.EXAMPLE
  pwsh tools/osiris/run-native-link.ps1 -Src samples/hello.c -Run HELLO -Expect 'Hello, Osiris'
.EXAMPLE
  # Resolve the archives via the C68KLIB search path (staged in \LIB, not the CWD),
  # and rely on LINK's strip-by-default:
  pwsh tools/osiris/run-native-link.ps1 -Src samples/hello.c -Run HELLO -C68klib -Expect 'Hello, Osiris'
.EXAMPLE
  # Also emit + verify the /map link map and the /sym sid68k symbol file:
  pwsh tools/osiris/run-native-link.ps1 -Src samples/printftest.c -Run PRINTF -Map -Sym -Expect 'int=42'
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
  [string]$FloatLib = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lib\libm\libm.a'),
  [string]$HeapLib = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lib\heap\libheap.a'),
  [string]$Ar = 'C:\git\osiris\toolchain\binutils\m68k-elf-ar.exe',
  [string]$Ranlib = 'C:\git\osiris\toolchain\binutils\m68k-elf-ranlib.exe',
  [switch]$UseLib,          # archive extra TUs into EXTRA.A with LIB.PRG, link that
  [switch]$NoIntegrated,    # compile C via asm68K (isolate integrated-emitter issues)
  [switch]$Bare,            # link crt0/seam + program + runtime only (no archives)
  [switch]$NoFloat,         # link libc.a/libheap.a but not libm.a (isolate the float archive)
  [switch]$NoHeap,          # omit libheap.a (a program that never calls malloc/free)
  [switch]$NoStrip,         # keep the .symtab (unstripped) -- passes /NOSTRIP
  [switch]$C68klib,         # stage archives in \LIB + SET C68KLIB=\LIB (search demo, not CWD)
  [switch]$Map,             # also write a /map link map (<RUN>.MAP) + TYPE it to verify
  [switch]$Sym,             # also write a /sym sid68k symbol file (<RUN>.SYM) + TYPE it to verify
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
function Add-Fat12File($img, [string]$name11, [byte[]]$data, [int]$dirCluster = 0){
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
  # Directory to place the entry in: the fixed root region ($dirCluster 0) or a
  # subdirectory's data cluster (16 entries) for a C68KLIB staging dir.
  if ($dirCluster -eq 0) { $dirBase = $rootLba*512;                     $dirEnts = $RootEnts }
  else                   { $dirBase = ($dataLba + ($dirCluster-2))*512; $dirEnts = [int]($Bpc/32) }
  $slot = -1
  for ($i=0; $i -lt $dirEnts; $i++){
    $b = $img[$dirBase + $i*32]
    if ($b -eq 0x00 -or $b -eq 0xE5) { $slot = $i; break }
  }
  if ($slot -lt 0) { throw "Add-Fat12File: no free dir slot" }
  for ($i=0; $i -lt $ncl; $i++){
    $c = $chain[$i]
    $dst = ($dataLba + ($c-2))*512
    $n = [math]::Min($Bpc, $data.Length - $i*$Bpc)
    if ($n -gt 0) { [Array]::Copy($data, $i*$Bpc, $img, $dst, $n) }
    $nv = if ($i -lt ($ncl-1)) { $chain[$i+1] } else { 0xFFF }
    _fat12set $img $f1 $c $nv; _fat12set $img $f2 $c $nv
  }
  $r = $dirBase + $slot*32
  [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img, $r)
  $img[$r+0x0B] = 0x20
  _w16 $img ($r+0x1A) $chain[0]; _w32 $img ($r+0x1C) $data.Length
}
# Create a subdirectory in the root; returns its first data cluster (pass to
# Add-Fat12File -dirCluster). Used to stage the C68KLIB search dir (\LIB).
function Add-Fat12Dir($img, [string]$dirname){
  $FatSz=9; $RootEnts=224; $Bpc=512
  $f1 = 512; $f2 = (1+$FatSz)*512
  $rootLba = 1 + 2*$FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts*32/512.0)
  $dataLba = $rootLba + $rootSecs
  $maxCl = (2880 - $dataLba) + 1
  $dc = -1
  for ($c=2; $c -le $maxCl; $c++){ if ((_fat12get $img $f1 $c) -eq 0) { $dc=$c; break } }
  if ($dc -lt 0) { throw "Add-Fat12Dir: no free cluster" }
  _fat12set $img $f1 $dc 0xFFF; _fat12set $img $f2 $dc 0xFFF
  $cbase = ($dataLba + ($dc-2))*512
  for ($k=0; $k -lt $Bpc; $k++){ $img[$cbase+$k] = 0 }
  # '.' -> self, '..' -> root (cluster 0); both ATTR_DIRECTORY (0x10).
  $dot    = '.'  + (' '*10)
  $dotdot = '..' + (' '*9)
  [Text.Encoding]::ASCII.GetBytes($dot).CopyTo($img, $cbase)
  $img[$cbase+0x0B] = 0x10; _w16 $img ($cbase+0x1A) $dc; _w32 $img ($cbase+0x1C) 0
  [Text.Encoding]::ASCII.GetBytes($dotdot).CopyTo($img, $cbase+32)
  $img[$cbase+32+0x0B] = 0x10; _w16 $img ($cbase+32+0x1A) 0; _w32 $img ($cbase+32+0x1C) 0
  $slot = -1
  for ($i=0; $i -lt $RootEnts; $i++){ $b=$img[$rootLba*512+$i*32]; if ($b -eq 0x00 -or $b -eq 0xE5){ $slot=$i; break } }
  if ($slot -lt 0) { throw "Add-Fat12Dir: no free root-dir slot" }
  $r = $rootLba*512 + $slot*32
  $n11 = ((($dirname.ToUpper()).PadRight(8)).Substring(0,8)) + (' '*3)
  [Text.Encoding]::ASCII.GetBytes($n11).CopyTo($img, $r)
  $img[$r+0x0B] = 0x10; _w16 $img ($r+0x1A) $dc; _w32 $img ($r+0x1C) 0
  return $dc
}
# Create a fresh, blank 1.44 MB FAT12 image for the B: data floppy (~1.42 MB
# free -- room the grown archives no longer have on the boot floppy). BPB from
# the osiris link-oracle New-Fat12Image; proven to mount as B: under Osiris.
function New-Fat12Image {
  $img = New-Object 'byte[]' (2880*512)
  $img[0]=0xEB; $img[1]=0x3C; $img[2]=0x90
  [Text.Encoding]::ASCII.GetBytes('C68KLINK').CopyTo($img,3)
  $img[11]=0x00; $img[12]=0x02; $img[13]=1                       # 512 B/sector, 1 sec/clus
  $img[14]=1; $img[15]=0                                          # 1 reserved sector
  $img[16]=2                                                      # 2 FATs
  $img[17]=0xE0; $img[18]=0x00                                    # 224 root entries
  $img[19]=0x40; $img[20]=0x0B                                    # 2880 total sectors
  $img[21]=0xF0                                                   # media descriptor
  $img[22]=9; $img[23]=0                                          # 9 sectors/FAT
  $img[24]=18; $img[25]=0; $img[26]=2; $img[27]=0                 # 18 sec/track, 2 heads
  $img[38]=0x29; $img[39]=0x11; $img[40]=0x22; $img[41]=0x33; $img[42]=0x44
  [Text.Encoding]::ASCII.GetBytes('C68K DATA  ').CopyTo($img,43) # 11-byte volume label
  [Text.Encoding]::ASCII.GetBytes('FAT12   ').CopyTo($img,54)
  $img[510]=0x55; $img[511]=0xAA
  foreach($fat in @(1,10)){ $o=$fat*512; $img[$o]=0xF0; $img[$o+1]=0xFF; $img[$o+2]=0xFF }
  return ,$img
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
foreach($p in @($sim,$rom,$baseImg,$LinkPrg)){ if(-not (Test-Path $p)){ throw "run-native-link: missing '$p'" } }

# ---- build the objects (crt0/rt via asm68K; C via c68k integrated emitter) ----
$work=Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-native-link'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$inc=Join-Path $repo 'libc\include'
$sysO=Join-Path $work 'SYS.O'; $rtO=Join-Path $work 'RT68K.O'; $libcA=Join-Path $work 'libc.a'; $progO=Join-Path $work "$Run.O"
$sysA=Join-Path $repo 'libc\osiris\osiris_sys.a68'; $rtA=Join-Path $repo 'lib\runtime\rt68k.a68'
$ccArgs=@(); if (-not $NoIntegrated) { $ccArgs += '-fintegrated-as' }
function Chk($desc){ if($LASTEXITCODE -ne 0){ throw "$desc failed (rc=$LASTEXITCODE)" } }
& $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA  2>&1 | Out-Null; Chk 'asm crt0'
& $Asm /Cx /elf /c /nologo "/Fo$rtO"  $rtA   2>&1 | Out-Null; Chk 'asm runtime'
# The native Osiris LINK member-selects per object from the c68k archives --
# the same dead-stripping the cross ld does. LINK.PRG now searches MULTIPLE
# archives per link (fixpoint over all archives), so libc.a/libm.a/libheap.a
# are staged and linked as three separate archives -- no host-side merge.
# Build libc.a here; libm.a/libheap.a come from the tree (built on demand).
if (-not $Bare) {
  & (Join-Path $repo 'tools\build-libc.ps1') -OutDir $work | Out-Null; Chk 'build libc.a'
  if (-not (Test-Path $FloatLib)) { & (Join-Path $repo 'tools\build-libm.ps1')   | Out-Null; Chk 'build libm.a' }
  if (-not (Test-Path $HeapLib))  { & (Join-Path $repo 'tools\build-libheap.ps1') | Out-Null; Chk 'build libheap.a' }
}
& $Cc @ccArgs -c $Src   -o $progO "-I$inc"    2>&1 | Out-Null; Chk 'cc program'

# extra translation units (multi-object / archive demo)
$extraObjs=@(); $ei=0
foreach($ex in $Extra){
  $exO=Join-Path $work ("EX{0}.O" -f $ei)
  & $Cc @ccArgs -c (Resolve-Path $ex).Path -o $exO "-I$inc" 2>&1 | Out-Null; Chk "cc extra $ex"
  $extraObjs += @{ N=("EX{0}.O" -f $ei); F=$exO }; $ei++
}

# ---- stage a fresh B: data floppy (fd1); A: stays the pristine boot floppy ----
# The grown archives (libc.a+libm.a+libheap.a ~1 MB) no longer fit beside the OS
# on the 1.44 MB boot floppy, so the toolchain + inputs go on a fresh B: data
# floppy (~1.42 MB free) and the link runs there. Osiris maps fd1 -> B:.
Stop-AllSim
$fd0=Join-Path $work 'a.img'; $img=Join-Path $work 'b.img'; $log=Join-Path $work 'con.log'; $rtc=Join-Path $work 'rtc.nv'
Copy-Item $baseImg $fd0 -Force
$bz=New-Fat12Image
$stage=@(
  @{ N='LINK.PRG'; F=$LinkPrg },
  @{ N='SYS.O';    F=$sysO },
  @{ N='RT68K.O';  F=$rtO },
  @{ N="$Run.O";   F=$progO }
)
foreach($eo in $extraObjs){ $stage += $eo }
$arch = @()
if (-not $Bare) {
  $arch += @{ N='LIBC.A';    F=$libcA }
  if (-not $NoFloat) { $arch += @{ N='LIBM.A';    F=$FloatLib } }
  if (-not $NoHeap)  { $arch += @{ N='LIBHEAP.A'; F=$HeapLib } }
  # Default: archives sit in B:\ (the CWD). With -C68klib they go into B:\LIB
  # instead, so the link must resolve them via the C68KLIB search.
  if (-not $C68klib) { $stage += $arch }
}
if ($UseLib) { $stage += @{ N='LIB.PRG'; F=$LibPrg } }
foreach($s in $stage){ Add-Fat12File $bz (Name11 $s.N) ([IO.File]::ReadAllBytes($s.F)) }
if ($C68klib -and -not $Bare) {
  $libcl = Add-Fat12Dir $bz 'LIB'
  foreach($s in $arch){ Add-Fat12File $bz (Name11 $s.N) ([IO.File]::ReadAllBytes($s.F)) $libcl }
}
[IO.File]::WriteAllBytes($img,$bz)
Remove-Item $log -ErrorAction SilentlyContinue
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc,(New-Object byte[] 64)) }

# ---- boot + drive the shell ----
$psi=New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName=$sim
$simArgs=@()
if ($Cpu) { $simArgs += @('--cpu',$Cpu) }
if ($Mem) { $simArgs += @('--mem',$Mem) }
$simArgs += @("--rom:$rom",'--fd0',$fd0,'--fd1',$img,'--acia-port','none','--fdc-threads','off','--rtc-nv',$rtc,'--tee-acia',$log)
foreach($a in $simArgs){ [void]$psi.ArgumentList.Add($a) }
$psi.RedirectStandardInput=$true; $psi.UseShellExecute=$false
$p=[System.Diagnostics.Process]::Start($psi)
function _send($proc,[string]$s){ $b=[Text.Encoding]::ASCII.GetBytes($s); $proc.StandardInput.BaseStream.Write($b,0,$b.Length); $proc.StandardInput.BaseStream.Flush() }

# object list for LINK: crt0, program, extras (or their archive), runtime, [libc/libm/libheap archives]
$linkObjs = @('SYS.O', "$Run.O")
$useArchive = ($UseLib -and $extraObjs.Count -gt 0)
if ($useArchive) { $linkObjs += 'EXTRA.A' }
else { foreach($eo in $extraObjs){ $linkObjs += $eo.N } }
$linkObjs += 'RT68K.O'
if (-not $Bare) { $linkObjs += 'LIBC.A'; if (-not $NoFloat) { $linkObjs += 'LIBM.A' }; if (-not $NoHeap) { $linkObjs += 'LIBHEAP.A' } }
# Strip is the native LINK default (R5); -NoStrip passes /NOSTRIP to keep the
# full .symtab (LINK sizes it to the actual symbol count).
$sflag = if ($NoStrip) { '/NOSTRIP ' } else { '' }
# R6/R7 opt-in companion files: a /map link map and a /sym sid68k symbol file
# (both written beside the .PRG, byte-neutral to the executable).
$outflags = ''
if ($Map) { $outflags += "/map:$Run.MAP " }
if ($Sym) { $outflags += "/sym:$Run.SYM " }
$linkCmd = "LINK ${sflag}${outflags}-o $Run.PRG " + ($linkObjs -join ' ')
try {
  Start-Sleep -Seconds $BootWait
  _send $p ("B:`r"); Start-Sleep -Seconds 1   # switch to the B: data floppy (the work drive)
  if ($C68klib) {
    # The archives live in B:\LIB, not the CWD: point the linker's library
    # search there so the bare LIBC.A/... names resolve via C68KLIB.
    _send $p ("SET C68KLIB=\LIB`r"); Start-Sleep -Seconds 2
    Write-Host 'SET C68KLIB=\LIB (B:\LIB)' -ForegroundColor DarkCyan
  }
  if ($useArchive) {
    $libCmd = 'LIB rcs EXTRA.A ' + (($extraObjs | ForEach-Object { $_.N }) -join ' ')
    _send $p ("{0}`r" -f $libCmd); Start-Sleep -Seconds 5
    Write-Host "LIB cmd: $libCmd" -ForegroundColor DarkCyan
  }
  _send $p ("{0}`r" -f $linkCmd)
  Start-Sleep -Seconds $LinkWait
  if ($Map) { _send $p ("TYPE $Run.MAP`r"); Start-Sleep -Seconds 2 }
  if ($Sym) { _send $p ("TYPE $Run.SYM`r"); Start-Sleep -Seconds 2 }
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
# Verify the R6/R7 companion files were produced (TYPEd to the console above).
if ($Map) {
  if ($logText -match 'LINK\.PRG map for' -or $logText -match 'Section\s+Address\s+Size') { Write-Host 'NATIVE: /map OK' -ForegroundColor Green }
  else { Write-Host 'NATIVE: /map MISSING' -ForegroundColor Red; $rc=1 }
}
if ($Sym) {
  if ($logText -match 'LINK\.PRG symbols for' -or $logText -match '(?im)^[0-9A-Fa-f]{8}\s+[TDBAtdba]\s') { Write-Host 'NATIVE: /sym OK' -ForegroundColor Green }
  else { Write-Host 'NATIVE: /sym MISSING' -ForegroundColor Red; $rc=1 }
}
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if (($Expect.Count -gt 0 -or $Map -or $Sym) -and $rc -eq 0){ Write-Host "NATIVE: PASS" -ForegroundColor Green }
exit $rc
