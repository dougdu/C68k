#requires -version 5
<#
.SYNOPSIS
  Build the native c68k compiler CC.68K (stage2) for CP/M-68K.

.DESCRIPTION
  Cross-compiles the compiler's own source with the cross-compiler and the
  integrated ELF emitter under -DC68K_SELFHOST (native driver: no subprocess,
  no external assembler, no linker), then links the objects + the CP/M syscall
  seam (cpm.c) + libc + CP/M crt0 (cpm_sys.a68) + integer runtime + soft-float
  at the TPA base (0x500) via cpm68k.ld, and converts the ELF to a DRI
  contiguous transient with mkdri:

      m68k-elf-ld -T cpm68k.ld -Ttext 0x500 cpm_sys.o <compiler>.o... \
          cpm.o libc.o rt68k.o libm.a -o CC.elf
      mkdri -b500 -y -o CC.68K CC.elf

  cpm_sys.o MUST be first so _start lands at the TPA base (CP/M has no ENTRY).
  The compiler objects are OS-neutral ELF, so they are byte-identical to the
  Osiris stage2 objects; only the crt0/seam differ. This is stage2 of the P10
  self-hosting bootstrap for CP/M; running CC.68K under sim68k to recompile the
  same source yields stage3 (must be byte-identical).
#>
[CmdletBinding()]
param(
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Asm = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ld = 'C:\git\osiris\toolchain\binutils\m68k-elf-ld.exe',
  [string]$Mkdri = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\mkdri.exe',
  [string]$LdScript = (Join-Path $PSScriptRoot 'cpm68k.ld'),
  [string]$FloatLib = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lib\libm\libm.a'),
  [string]$OutDir = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc68k')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$src  = Join-Path $repo 'src'
$inc  = Join-Path $repo 'libc\include'
$binc = Join-Path $repo 'include'
$sysA = Join-Path $repo 'libc\cpm\cpm_sys.a68'
$seamC = Join-Path $repo 'libc\cpm\cpm.c'
$rtA  = Join-Path $repo 'lib\runtime\rt68k.a68'
foreach ($t in @($Cc,$Asm,$Ld,$Mkdri)) { if (-not (Test-Path $t)) { throw "missing tool: $t" } }
foreach ($f in @($LdScript,$FloatLib,$sysA,$seamC,$rtA)) { if (-not (Test-Path $f)) { throw "missing input: $f" } }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# compat.c is host-only (spawn/open_memstream shims); the native build omits it.
$tus = @('main','strings','hashmap','unicode','type','tokenize','preprocess','parse','codegen68k','emit_elf')
$objs = @()
foreach ($n in $tus) {
  $o = Join-Path $OutDir "$n.o"
  Write-Host "cc   -DC68K_SELFHOST $n.c" -ForegroundColor Cyan
  & $Cc -fintegrated-as -DC68K_SELFHOST -c "-I$binc" "-I$inc" "-I$src" -o $o (Join-Path $src "$n.c")
  if ($LASTEXITCODE -ne 0) { throw "cc $n failed (rc=$LASTEXITCODE)" }
  $objs += $o
}

$seamO = Join-Path $OutDir 'cpm.o'
$sysO  = Join-Path $OutDir 'cpm_sys.o'
$rtO   = Join-Path $OutDir 'rt68k.o'
Write-Host 'cc   cpm.c (CP/M seam)' -ForegroundColor Cyan
& $Cc -fintegrated-as -c "-I$inc" -o $seamO $seamC
if ($LASTEXITCODE -ne 0) { throw 'cc cpm seam failed' }
# Phase 4: compile the split libc TUs (libc/core/*.c) into libc.a; link via -lc.
Write-Host 'build libc.a (split TUs)' -ForegroundColor Cyan
$buildLibc = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-libc.ps1'
& $buildLibc -Cc $Cc -OutDir $OutDir -CcArgs @('-fintegrated-as') | Out-Null
Write-Host 'asm  cpm_sys.a68 ; rt68k.a68' -ForegroundColor Cyan
& $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'asm crt0 failed' }
& $Asm /Cx /elf /c /nologo "/Fo$rtO" $rtA | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'asm rt68k failed' }

$elf = Join-Path $OutDir 'CC.elf'
$out68 = Join-Path $OutDir 'CC.68K'
Write-Host 'ld   -> CC.elf' -ForegroundColor Cyan
& $Ld -T $LdScript -Ttext 0x500 -o $elf $sysO @objs $seamO "-L$OutDir" -lc $rtO $FloatLib
if ($LASTEXITCODE -ne 0) { throw "link CC.elf failed (rc=$LASTEXITCODE)" }
Write-Host 'mkdri -> CC.68K' -ForegroundColor Cyan
& $Mkdri -b500 -y -o $out68 $elf | Out-Null
if ($LASTEXITCODE -ne 0) { throw "mkdri CC.68K failed (rc=$LASTEXITCODE)" }

Write-Host ("OK: {0}  ({1:n0} bytes)" -f $out68, (Get-Item $out68).Length) -ForegroundColor Green
$out68
