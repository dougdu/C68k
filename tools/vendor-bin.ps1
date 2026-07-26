#requires -version 5
<#
.SYNOPSIS
  Vendor the committed host toolchain binaries into tools/bin: the asm68K
  assembler and the GNU m68k-elf-ld linker (+ its one runtime DLL).

.DESCRIPTION
  c68k emits Motorola-syntax assembly assembled by asm68K into ELF32-BE objects,
  and links Osiris .PRG / CP/M .68K with GNU m68k-elf-ld (2.44+). Both live in
  their own sibling repos; this copies them under tools/bin (committed) so a bare
  clone builds without those repos present. asm68K.exe depends only on the system
  VC++ runtime; m68k-elf-ld.exe needs libwinpthread-1.dll (the rest of the MinGW
  DLLs beside it are for gdb, not ld).

  Re-run after rebuilding asm68K (C:\git\asm68K) to refresh the committed copy,
  then `git add tools/bin` + commit. The build scripts default $Asm/$Ld here.

.EXAMPLE
  pwsh tools/vendor-bin.ps1
#>
[CmdletBinding()]
param(
  [string]$Asm   = 'C:\git\asm68K\build\bin\Release\asm68K.exe',
  [string]$LdDir = 'C:\git\worm68k\tools\elf'   # m68k-elf-ld.exe + libwinpthread-1.dll
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$bin  = Join-Path $repo 'tools\bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null

function Copy-Tool([string]$src, [string]$name) {
  if (-not (Test-Path $src)) { throw "vendor-bin: source not found: $src" }
  Copy-Item $src (Join-Path $bin $name) -Force
  Write-Host ("vendor-bin: {0,-20} <- {1}  ({2:N0} bytes)" -f $name, $src, (Get-Item $src).Length) -ForegroundColor Green
}

Copy-Tool $Asm 'asm68K.exe'
Copy-Tool (Join-Path $LdDir 'm68k-elf-ld.exe')     'm68k-elf-ld.exe'
Copy-Tool (Join-Path $LdDir 'libwinpthread-1.dll') 'libwinpthread-1.dll'

Write-Host "vendor-bin: done -> $bin  (git add tools/bin + commit to publish)" -ForegroundColor Cyan
