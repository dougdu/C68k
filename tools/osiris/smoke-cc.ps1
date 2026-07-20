#requires -version 5
<#
.SYNOPSIS
  P10 stage2 smoke test: run the self-hosted compiler CC.PRG on Osiris (under
  sim68k) and prove it compiles a C source to a valid ELF object on-target.

.DESCRIPTION
  CC.PRG is the ~440 KB native compiler the cross-compiler built from c68k's
  own source (tools/osiris/build-cc.ps1).  This harness:

      1. cross-compiles the sample with the host c68k (-fintegrated-as -c)
         to a REFERENCE object -- same emit_elf code path CC uses on-target;
      2. stages CC.PRG + the sample .c on one FAT12 boot floppy;
      3. boots Osiris, types `CC -c X.C -o X.O`, then `DIR`;
      4. reads the floppy back, extracts X.O, and byte-compares it to the
         reference.

  A byte-identical match means the compiler running natively on the 68000
  produces exactly the object the cross-compiler does -- a per-file
  stage2 == stage3 result, and the go/no-go for the full self-recompile.

  The 68000 16 MB model (--cpu 68000 --mem MAX) is the default: the 1 MB
  model is too tight for a 440 KB program plus the compiler's arena.

.EXAMPLE
  pwsh tools/osiris/smoke-cc.ps1
  pwsh tools/osiris/smoke-cc.ps1 -Src samples/hello.c -KeepArtifacts
#>
[CmdletBinding()]
param(
  [string]$Src = '',        # C source to compile on-target (default: a generated self-contained sample)
  [string]$CcPrg = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc\CC.PRG'),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Cpu = '68000',   # 24-bit / 16 MB model (the compiler + arena needs room)
  [string]$Mem = 'MAX',
  [int]$BootWait = 5,
  [int]$CompileWait = 45,
  [int]$DirWait = 4,
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'

# ---- FAT12 helpers (add/remove verbatim from run-osiris.ps1; +Read below) ----
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
# Extract a file's bytes from the image (walks the cluster chain).
function Read-Fat12File($img, [string]$name11){
  $FatSz=9; $RootEnts=224; $Bpc=512
  $f1 = 512
  $rootLba = 1 + 2*$FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts*32/512.0)
  $dataLba = $rootLba + $rootSecs
  for ($i=0; $i -lt $RootEnts; $i++){
    $r = $rootLba*512 + $i*32
    $b = $img[$r]
    if ($b -eq 0x00 -or $b -eq 0xE5) { continue }
    if ([Text.Encoding]::ASCII.GetString($img, $r, 11) -ne $name11) { continue }
    $size = ($img[$r+0x1C] -band 0xFF) -bor (($img[$r+0x1D] -band 0xFF) -shl 8) `
          -bor (($img[$r+0x1E] -band 0xFF) -shl 16) -bor (($img[$r+0x1F] -band 0xFF) -shl 24)
    $cl = ($img[$r+0x1A] -band 0xFF) -bor (($img[$r+0x1B] -band 0xFF) -shl 8)
    $out = New-Object byte[] $size
    $got = 0
    while ($cl -ge 2 -and $cl -lt 0xFF0 -and $got -lt $size){
      $src = ($dataLba + ($cl-2))*512
      $n = [math]::Min($Bpc, $size - $got)
      [Array]::Copy($img, $src, $out, $got, $n)
      $got += $n
      $cl = _fat12get $img $f1 $cl
    }
    return ,$out
  }
  return $null
}
function Stop-AllSim {
  $deadline=(Get-Date).AddMilliseconds(6000)
  while((Get-Date) -lt $deadline){ $ps=Get-Process c68k-sim68k -ErrorAction SilentlyContinue; if(-not $ps){break}; $ps|Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 200 }
  Start-Sleep -Milliseconds 600
}

# ---- ensure simenv + CC.PRG ----
if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) { & (Join-Path $repo 'tools\bootstrap-simenv.ps1') }
$sim=Join-Path $simenv 'c68k-sim68k.exe'; $rom=Join-Path $simenv 'bootrom.bin'; $baseImg=Join-Path $simenv 'osiris-boot-144.img'
if (-not (Test-Path $CcPrg)) {
  Write-Host "CC.PRG not found at $CcPrg -- building it (build-cc.ps1)..." -ForegroundColor Yellow
  & (Join-Path $PSScriptRoot 'build-cc.ps1')
  if ($LASTEXITCODE -ne 0) { throw "build-cc.ps1 failed" }
}
foreach($p in @($sim,$rom,$baseImg,$CcPrg,$Cc)){ if(-not (Test-Path $p)){ throw "smoke-cc: missing '$p'" } }

$work=Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-smoke-cc'
New-Item -ItemType Directory -Force -Path $work | Out-Null

# ---- the on-target source (self-contained: no #include, compiled to .o only) ----
$srcName = 'SMOKE.C'
if ($Src) {
  $Src = (Resolve-Path $Src).Path
  $srcName = [IO.Path]::GetFileName($Src).ToUpper()
}
$srcC = Join-Path $work $srcName
if ($Src) {
  Copy-Item $Src $srcC -Force
} else {
  @'
/* Self-contained smoke source: exercises globals, a loop, locals, calls,
   pointers and rodata -- and needs no headers (compiled to .o, not linked). */
int g_table[4] = { 1, 2, 3, 4 };
const char *g_msg = "smoke";

static int sum_to(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += i;
  return s;
}

static int dot(const int *a, const int *b, int n) {
  int acc = 0;
  while (n-- > 0)
    acc += *a++ * *b++;
  return acc;
}

int compute(int x) {
  int y = sum_to(x);
  int z = dot(g_table, g_table, 4);
  return y + z + (int)g_msg[0];
}

int main(void) {
  return compute(10) & 0x7f;
}
'@ | Set-Content -Path $srcC -Encoding ASCII
}
$objName = [IO.Path]::ChangeExtension($srcName, '.O').ToUpper()

# ---- reference object: host c68k, same integrated emit_elf path CC uses ----
# Compile from the work dir with the same relative name Osiris sees (SMOKE.C),
# so base_file matches the on-target run and the objects can be byte-compared.
$refObj = Join-Path $work 'REF.O'
Push-Location $work
try {
  & $Cc -fintegrated-as -c $srcName -o 'REF.O' 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "host cross-compile of $srcName failed (rc=$LASTEXITCODE)" }
} finally { Pop-Location }
$refBytes = [IO.File]::ReadAllBytes($refObj)
Write-Host ("reference .o (host c68k -fintegrated-as): {0} bytes" -f $refBytes.Length) -ForegroundColor DarkCyan

# ---- stage floppy: CC.PRG + the source ----
Stop-AllSim
$img=Join-Path $work 'os.img'; $log=Join-Path $work 'con.log'; $rtc=Join-Path $work 'rtc.nv'
$bz=[IO.File]::ReadAllBytes($baseImg)
$stage=@(
  @{ N='CC.PRG'; F=$CcPrg },
  @{ N=$srcName; F=$srcC }
)
foreach($s in $stage){ $n=Name11 $s.N; Remove-Fat12File $bz $n; Add-Fat12File $bz $n ([IO.File]::ReadAllBytes($s.F)) }
# make sure any stale output from a previous run is gone
Remove-Fat12File $bz (Name11 $objName)
Remove-Fat12File $bz (Name11 'CC_TMP.S')
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

$ccCmd = "CC -c $srcName -o $objName"
try {
  Start-Sleep -Seconds $BootWait
  _send $p ("{0}`r" -f $ccCmd)
  Start-Sleep -Seconds $CompileWait
  _send $p ("DIR`r")
  Start-Sleep -Seconds $DirWait
} finally {
  try { $p.Kill() } catch {}
  $p.WaitForExit(); Stop-AllSim
}

$logText = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
Write-Host "===== Osiris console ====="
Write-Host $logText
Write-Host "=========================="
Write-Host "CC cmd: $ccCmd" -ForegroundColor DarkCyan

# ---- extract the on-target object + compare ----
$outBz=[IO.File]::ReadAllBytes($img)
$objBytes = Read-Fat12File $outBz (Name11 $objName)
$rc = 0
if ($null -eq $objBytes) {
  Write-Host "SMOKE: FAIL -- CC produced no $objName on the floppy" -ForegroundColor Red
  $rc = 1
} else {
  if ($KeepArtifacts) { [IO.File]::WriteAllBytes((Join-Path $work $objName), $objBytes) }
  $elf = ($objBytes.Length -ge 4 -and $objBytes[0] -eq 0x7F -and $objBytes[1] -eq 0x45 -and $objBytes[2] -eq 0x4C -and $objBytes[3] -eq 0x46)
  Write-Host ("on-target .o: {0} bytes, ELF magic {1}" -f $objBytes.Length, ($(if($elf){'OK'}else{'MISSING'}))) -ForegroundColor DarkCyan
  if (-not $elf) {
    Write-Host "SMOKE: FAIL -- $objName is not an ELF object" -ForegroundColor Red
    $rc = 1
  } elseif ($objBytes.Length -ne $refBytes.Length) {
    Write-Host ("SMOKE: FAIL -- size differs (target {0} vs host {1})" -f $objBytes.Length, $refBytes.Length) -ForegroundColor Red
    $rc = 1
  } else {
    $diff = -1
    for ($i=0; $i -lt $refBytes.Length; $i++){ if ($objBytes[$i] -ne $refBytes[$i]){ $diff=$i; break } }
    if ($diff -ge 0) {
      Write-Host ("SMOKE: FAIL -- first byte diff at offset {0} (target 0x{1:X2} vs host 0x{2:X2})" -f $diff, $objBytes[$diff], $refBytes[$diff]) -ForegroundColor Red
      $rc = 1
    } else {
      Write-Host "SMOKE: PASS -- CC.PRG on-target object is byte-identical to the host cross-compiler" -ForegroundColor Green
    }
  }
}

if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
exit $rc
