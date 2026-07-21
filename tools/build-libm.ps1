#requires -version 5
<#
.SYNOPSIS
  Build the vendored IEEE-754 math library into lib/libm/libm.a.

.DESCRIPTION
  Assembles every .a68 under lib/libm/{core,conv,fmt,math} with asm68K (the same
  assembler used for the crt0/seam and integer runtime) and archives them into a
  single libm.a (single + double precision). The target link steps consume it via
  -lm; archive member selection means non-float programs pull nothing.

  PROVENANCE: vendored 2026-07-21 from the worm68k IEEE-754 float library
    C:\git\worm68k\68kTools\libraries\float\ieee754   (28 .a68 sources + 2 .inc)
  libm.a is the double-capable archive, equivalent to the upstream libieee754d.a
  (ALL_OBJS = single + double). Rebuild after syncing the vendored sources.

.EXAMPLE
  pwsh tools/build-libm.ps1
#>
[CmdletBinding()]
param(
  [string]$Asm    = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ar     = 'C:\git\osiris\toolchain\binutils\m68k-elf-ar.exe',
  [string]$Ranlib = 'C:\git\osiris\toolchain\binutils\m68k-elf-ranlib.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$libm = Join-Path $repo 'lib\libm'
$outA = Join-Path $libm 'libm.a'

foreach ($t in @($Asm, $Ar, $Ranlib)) {
  if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "build-libm: tool not found: $t" }
}

$objDir = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-libm-obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
Get-ChildItem $objDir -Filter *.o -ErrorAction SilentlyContinue | Remove-Item -Force

# Assemble from the libm root so `include ieee754.inc` resolves via /I. (matches
# the upstream Makefile's invocation exactly).
$objs = @()
Push-Location $libm
try {
  foreach ($s in (Get-ChildItem -Path core, conv, fmt, math -Filter *.a68 -Recurse)) {
    $o = Join-Path $objDir ($s.BaseName + '.o')
    & $Asm /Cx /elf /c /nologo /I. "/Fo$o" $s.FullName 2>&1 | Out-String -OutVariable out | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host $out; throw "build-libm: asm failed: $($s.Name)" }
    $objs += $o
  }
}
finally { Pop-Location }

Remove-Item $outA -Force -ErrorAction SilentlyContinue
& $Ar rcs $outA @objs
if ($LASTEXITCODE -ne 0) { throw "build-libm: ar failed" }
& $Ranlib $outA
if ($LASTEXITCODE -ne 0) { throw "build-libm: ranlib failed" }

Write-Host ("build-libm: {0}  ({1} bytes, {2} objects)" -f $outA, (Get-Item $outA).Length, $objs.Count) -ForegroundColor Green
$outA
