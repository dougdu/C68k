#requires -version 5
<#
.SYNOPSIS
  Package the c68k cross-compiler SDK into a self-contained, distributable
  staging tree (and a .zip) for third-party program development.

.DESCRIPTION
  Stages the assets THIS repository owns:

      bin/    the c68k cross-compiler (built here or passed via -Cc)
      include/  freestanding builtin headers (found via <argv0dir>/include)
      libc/   the C library sources (core + per-OS seam + crt0) and headers
      lib/    the integer runtime (soft-float / long long / helpers)
      docs/   the SDK guide + the libc/toolchain reference
      LICENSE, src/CHIBICC-LICENSE

  The external toolchain used at LINK time -- m68k-elf-ld, mkdri, asm68K, and
  the sim -- lives in the sibling worm68k / osiris repos; docs/sdk.md lists it
  as a prerequisite. This script deliberately does NOT vendor those binaries.

.NOTES
  A reasonable default layout; adjust as the distribution story firms up.
#>
[CmdletBinding()]
param(
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$OutDir = '',
  [string]$Version = '0.1.0',
  [switch]$Zip
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $OutDir) { $OutDir = Join-Path $repo 'dist' }

if (-not (Test-Path $Cc)) {
  throw "package: c68k not found at '$Cc'. Build it first (see Makefile / CMake) or pass -Cc."
}

$stage = Join-Path $OutDir "c68k-sdk-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$null = New-Item -ItemType Directory -Force -Path (Join-Path $stage 'bin')

# --- compiler binary ---
Copy-Item $Cc (Join-Path $stage 'bin\c68k.exe') -Force

# --- source/header trees this repo owns ---
foreach ($d in 'include', 'libc', 'lib') {
  Copy-Item (Join-Path $repo $d) (Join-Path $stage $d) -Recurse -Force
}

# --- docs + licenses ---
$null = New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs')
foreach ($f in 'docs\sdk.md', 'docs\libc-and-toolchain.md', 'docs\architecture.md') {
  Copy-Item (Join-Path $repo $f) (Join-Path $stage 'docs') -Force
}
Copy-Item (Join-Path $repo 'LICENSE') $stage -Force
Copy-Item (Join-Path $repo 'src\CHIBICC-LICENSE') (Join-Path $stage 'CHIBICC-LICENSE') -Force

# --- top-level pointer ---
@"
c68k SDK $Version
=================

A C99 cross-compiler for the Motorola 68000, targeting Osiris DOS (OS/68K) and
CP/M-68K from one source tree.

  bin/c68k.exe   the cross-compiler
  include/       freestanding builtin headers
  libc/, lib/    C library + runtime sources
  docs/sdk.md    START HERE -- how to build your program

Linking needs the external toolchain (m68k-elf-ld, mkdri, asm68K) and the
sim68k simulator; see docs/sdk.md for the prerequisites and link recipes.

Based on chibicc (MIT); see CHIBICC-LICENSE.
"@ | Set-Content (Join-Path $stage 'README.txt') -Encoding ASCII

Write-Host "staged: $stage" -ForegroundColor Green

if ($Zip) {
  $zipPath = "$stage.zip"
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Compress-Archive -Path $stage -DestinationPath $zipPath
  Write-Host "zipped: $zipPath" -ForegroundColor Green
}
