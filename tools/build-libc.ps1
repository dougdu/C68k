#requires -version 5
<#
.SYNOPSIS
  Compile the split libc translation units (libc/core/*.c) into libc.a.

.DESCRIPTION
  Phase 4 splits the former monolithic libc.c into per-section (heading toward
  per-function) source files so the linker dead-strips unused libc objects.
  This compiles every libc/core/*.c with c68k and archives the objects into a
  single libc.a; consumers link it with -lc. New carved files are picked up
  automatically (glob), so later granularity increments need no build change.

  Object names are prefixed 'libc__' so they never collide with a program's
  own object of the same basename in a shared output directory.
#>
[CmdletBinding()]
param(
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$OutDir = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-libc-obj'),
  [string]$Ar = 'C:\git\osiris\toolchain\binutils\m68k-elf-ar.exe',
  [string]$Ranlib = 'C:\git\osiris\toolchain\binutils\m68k-elf-ranlib.exe',
  [string[]]$CcArgs = @('-fintegrated-as')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$core = Join-Path $repo 'libc\core'
$inc  = Join-Path $repo 'libc\include'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$libcA = Join-Path $OutDir 'libc.a'
Get-ChildItem $OutDir -Filter 'libc__*.o' -ErrorAction SilentlyContinue | Remove-Item -Force

$objs = @()
foreach ($s in (Get-ChildItem $core -Filter *.c | Sort-Object Name)) {
  $o = Join-Path $OutDir ('libc__' + $s.BaseName + '.o')
  & $Cc @CcArgs -c "-I$inc" -o $o $s.FullName 2>&1 | Out-String -OutVariable out | Out-Null
  if ($LASTEXITCODE -ne 0) { Write-Host $out; throw "build-libc: cc $($s.Name) failed" }
  $objs += $o
}

Remove-Item $libcA -Force -ErrorAction SilentlyContinue
& $Ar rcs $libcA @objs
if ($LASTEXITCODE -ne 0) { throw 'build-libc: ar failed' }
& $Ranlib $libcA
if ($LASTEXITCODE -ne 0) { throw 'build-libc: ranlib failed' }

Write-Host ("build-libc: {0}  ({1} bytes, {2} objects)" -f $libcA, (Get-Item $libcA).Length, $objs.Count) -ForegroundColor Green
$libcA
