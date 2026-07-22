#requires -version 5
<#
.SYNOPSIS
  Build the native c68k compiler CC.PRG (stage2) for Osiris.

.DESCRIPTION
  Cross-compiles the compiler's own source with the cross-compiler and the
  integrated ELF emitter under -DC68K_SELFHOST (native driver: no subprocess,
  no external assembler, no linker), then links the objects + libc + crt0/seam
  + integer runtime + soft-float archive into a directly-loadable .PRG:

      m68k-elf-ld -pie --no-dynamic-linker -z max-page-size=0x20 -s \
          -T osiris-prg.ld  osiris_sys.o <compiler>.o... libc.o rt68k.o \
          libm.a

  This is stage2 of the P10 self-hosting bootstrap; running CC.PRG under sim68k
  to recompile the same source yields stage3 (must be byte-identical).
#>
[CmdletBinding()]
param(
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Asm = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ld = 'C:\git\osiris\toolchain\binutils\m68k-elf-ld.exe',
  [string]$LdScript = 'C:\git\osiris\ld\osiris-prg.ld',
  [string]$FloatLib = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lib\libm\libm.a'),
  [string]$OutDir = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$src  = Join-Path $repo 'src'
$inc  = Join-Path $repo 'libc\include'
$binc = Join-Path $repo 'include'
$sysA = Join-Path $repo 'libc\osiris\osiris_sys.a68'
$rtA  = Join-Path $repo 'lib\runtime\rt68k.a68'
foreach ($t in @($Cc,$Asm,$Ld)) { if (-not (Test-Path $t)) { throw "missing tool: $t" } }
foreach ($f in @($LdScript,$FloatLib,$sysA,$rtA)) { if (-not (Test-Path $f)) { throw "missing input: $f" } }
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

$sysO  = Join-Path $OutDir 'osiris_sys.o'
$rtO   = Join-Path $OutDir 'rt68k.o'
# Phase 4: compile the split libc TUs (libc/core/*.c) into libc.a; link via -lc
# so CC.PRG dead-strips the libc it doesn't use.
Write-Host 'build libc.a (split TUs)' -ForegroundColor Cyan
$buildLibc = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-libc.ps1'
& $buildLibc -Cc $Cc -OutDir $OutDir -CcArgs @('-fintegrated-as') | Out-Null
Write-Host 'asm  osiris_sys.a68 ; rt68k.a68' -ForegroundColor Cyan
& $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'asm crt0 failed' }
& $Asm /Cx /elf /c /nologo "/Fo$rtO" $rtA | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'asm rt68k failed' }

$prg = Join-Path $OutDir 'CC.PRG'
Write-Host 'ld   -> CC.PRG' -ForegroundColor Cyan
# libheap backs malloc/free/realloc; link it (a non-allocating program
# dead-strips it entirely).  Build it on demand if missing.
$heapLib = Join-Path $repo 'lib\heap\libheap.a'
if (-not (Test-Path $heapLib)) { & (Join-Path (Split-Path $PSScriptRoot -Parent) 'build-libheap.ps1') | Out-Null }
$heapArgs = @($heapLib)
& $Ld -pie --no-dynamic-linker -z max-page-size=0x20 -s -T $LdScript -o $prg $sysO @objs "-L$OutDir" -lc $rtO $FloatLib @heapArgs
if ($LASTEXITCODE -ne 0) { throw "link CC.PRG failed (rc=$LASTEXITCODE)" }

Write-Host ("OK: {0}  ({1:n0} bytes)" -f $prg, (Get-Item $prg).Length) -ForegroundColor Green
