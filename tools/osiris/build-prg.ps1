#requires -version 5
<#
.SYNOPSIS
  Build a c68k C program into an Osiris .PRG (ELF32-MSB static PIE).

.DESCRIPTION
  Assembles the Osiris crt0/seam and the integer runtime, compiles the libc
  core and the program with c68k, and links them with the osiris-prg.ld script
  into a directly-loadable .PRG (the same recipe pascal68k uses):

      m68k-elf-ld -pie --no-dynamic-linker -z max-page-size=0x20 -s \
          -T osiris-prg.ld  osiris_sys.o prog.o libc.o rt68k.o libieee754d.a

  Link order puts the crt0/seam first; ENTRY(_start) fixes the entry regardless.
#>
param(
  [Parameter(Mandatory)][string]$Src,        # program .c source
  [string]$Name = '',                        # output basename (default: source stem, upper)
  [string]$Cc = 'c68k.exe',
  [string]$Asm = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ld = 'C:\git\osiris\toolchain\binutils\m68k-elf-ld.exe',
  [string]$LdScript = 'C:\git\osiris\ld\osiris-prg.ld',
  [string]$FloatLib = 'C:\git\worm68k\68kTools\libraries\float\ieee754\libieee754d.a',
  [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'

# Integrated ELF emitter (P8): set C68K_INTEGRATED_AS=1 to bypass asm68K for
# the C compiles (crt0/runtime .a68 still go through asm68K).
$asArgs = @()
if ($env:C68K_INTEGRATED_AS -eq '1') { $asArgs = @('-fintegrated-as') }

# Optimization level (P12): set C68K_OPT=1 to compile the libc + program at -O1.
$optArgs = @()
if ($env:C68K_OPT) { $optArgs = @("-O$($env:C68K_OPT)") }

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$inc  = Join-Path $repo 'libc\include'
$sysA = Join-Path $repo 'libc\osiris\osiris_sys.a68'
$rtA  = Join-Path $repo 'lib\runtime\rt68k.a68'
$libcC = Join-Path $repo 'libc\core\libc.c'

if (-not (Test-Path $Src)) { throw "build-prg: source not found: $Src" }
$Src = (Resolve-Path $Src).Path
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($Src).ToUpper() }

# Resolve c68k
if (-not (Get-Command $Cc -ErrorAction SilentlyContinue)) {
  foreach ($cand in @((Join-Path $repo 'build\c68k.exe'),
                      (Join-Path $repo 'c68k.exe'),
                      (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'))) {
    if (Test-Path $cand) { $Cc = $cand; break }
  }
}
foreach ($t in @($Cc, $Asm, $Ld)) {
  if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "build-prg: tool not found: $t" }
}
foreach ($f in @($LdScript, $FloatLib)) {
  if (-not (Test-Path $f)) { throw "build-prg: missing input: $f" }
}

if (-not $OutDir) { $OutDir = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-osiris' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Invoke-Step($desc, $sb) {
  & $sb 2>&1 | Out-String -OutVariable out | Out-Null
  if ($LASTEXITCODE -ne 0) { Write-Host $out; throw "$desc failed (rc=$LASTEXITCODE)" }
}

$sysO  = Join-Path $OutDir 'osiris_sys.o'
$rtO   = Join-Path $OutDir 'rt68k.o'
$libcO = Join-Path $OutDir 'libc.o'
$progO = Join-Path $OutDir "$Name.o"
$prg   = Join-Path $OutDir "$Name.PRG"

Invoke-Step 'asm crt0/seam' { & $Asm /Cx /elf /c /nologo "/Fo$sysO" $sysA }
Invoke-Step 'asm runtime'   { & $Asm /Cx /elf /c /nologo "/Fo$rtO"  $rtA }
Invoke-Step 'cc libc'       { & $Cc @asArgs @optArgs -c $libcC -o $libcO "-I$inc" }
Invoke-Step 'cc program'    { & $Cc @asArgs @optArgs -c $Src   -o $progO "-I$inc" }
Invoke-Step 'link .PRG'     { & $Ld -pie --no-dynamic-linker -z max-page-size=0x20 -s -T $LdScript $sysO $progO $libcO $rtO $FloatLib -o $prg }

Write-Host "build-prg: $prg" -ForegroundColor Green
$prg
