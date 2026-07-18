#requires -version 5
<#
.SYNOPSIS
  Build a c68k C program into a CP/M-68K transient (.68K).

.DESCRIPTION
  Assembles the CP/M crt0/BDOS primitive and the integer runtime, compiles the
  CP/M seam, the libc core, and the program with c68k, links them at the TPA
  base (0x500) via cpm68k.ld, then converts the ELF to a DRI contiguous .68K
  with mkdri (the same recipe pascal68k uses):

      m68k-elf-ld -T cpm68k.ld -Ttext 0x500 cpm_sys.o prog.o cpm.o libc.o \
          rt68k.o libieee754d.a -o prog.elf
      mkdri -b500 -y -o PROG.68K prog.elf

  cpm_sys.o MUST be first so _start lands at the TPA base (CP/M has no ENTRY).
#>
param(
  [Parameter(Mandatory)][string]$Src,
  [string]$Name = '',
  [string]$Cc = 'c68k.exe',
  [string]$Asm = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ld = 'C:\git\osiris\toolchain\binutils\m68k-elf-ld.exe',
  [string]$Mkdri = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\mkdri.exe',
  [string]$LdScript = (Join-Path $PSScriptRoot 'cpm68k.ld'),
  [string]$FloatLib = 'C:\git\worm68k\68kTools\libraries\float\ieee754\libieee754d.a',
  [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'

# Integrated ELF emitter (P8): set C68K_INTEGRATED_AS=1 to bypass asm68K for
# the C compiles (crt0/runtime .a68 still go through asm68K).
$asArgs = @()
if ($env:C68K_INTEGRATED_AS -eq '1') { $asArgs = @('-fintegrated-as') }

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$inc   = Join-Path $repo 'libc\include'
$sysA  = Join-Path $repo 'libc\cpm\cpm_sys.a68'
$seamC = Join-Path $repo 'libc\cpm\cpm.c'
$rtA   = Join-Path $repo 'lib\runtime\rt68k.a68'
$libcC = Join-Path $repo 'libc\core\libc.c'

if (-not (Test-Path $Src)) { throw "build-68k: source not found: $Src" }
$Src = (Resolve-Path $Src).Path
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($Src).ToUpper() }

if (-not (Get-Command $Cc -ErrorAction SilentlyContinue)) {
  foreach ($cand in @((Join-Path $repo 'build\c68k.exe'),
                      (Join-Path $repo 'c68k.exe'),
                      (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'))) {
    if (Test-Path $cand) { $Cc = $cand; break }
  }
}
foreach ($t in @($Cc, $Asm, $Ld, $Mkdri)) {
  if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "build-68k: tool not found: $t" }
}
foreach ($f in @($LdScript, $FloatLib)) {
  if (-not (Test-Path $f)) { throw "build-68k: missing input: $f" }
}

if (-not $OutDir) { $OutDir = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cpm' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Invoke-Step($desc, $sb) {
  & $sb 2>&1 | Out-String -OutVariable out | Out-Null
  if ($LASTEXITCODE -ne 0) { Write-Host $out; throw "$desc failed (rc=$LASTEXITCODE)" }
}

$sysO  = Join-Path $OutDir 'cpm_sys.o'
$rtO   = Join-Path $OutDir 'rt68k.o'
$seamO = Join-Path $OutDir 'cpm.o'
$libcO = Join-Path $OutDir 'libc.o'
$progO = Join-Path $OutDir "$Name.o"
$elf   = Join-Path $OutDir "$Name.elf"
$out68 = Join-Path $OutDir "$Name.68K"

Invoke-Step 'asm crt0/bdos' { & $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA }
Invoke-Step 'asm runtime'   { & $Asm /Cx /elf /c /nologo "/Fo$rtO"  $rtA }
Invoke-Step 'cc seam'       { & $Cc @asArgs -c $seamC -o $seamO "-I$inc" }
Invoke-Step 'cc libc'       { & $Cc @asArgs -c $libcC -o $libcO "-I$inc" }
Invoke-Step 'cc program'    { & $Cc @asArgs -c $Src   -o $progO "-I$inc" }
Invoke-Step 'link elf'      { & $Ld -T $LdScript -Ttext 0x500 $sysO $progO $seamO $libcO $rtO $FloatLib -o $elf }
Invoke-Step 'mkdri .68K'    { & $Mkdri -b500 -y -o $out68 $elf }

Write-Host "build-68k: $out68" -ForegroundColor Green
$out68
