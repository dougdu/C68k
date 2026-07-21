#requires -version 5
<#
.SYNOPSIS
  Build the vendored SOA heap library into lib/heap/libheap.a.

.DESCRIPTION
  Assembles every .a68 under lib/heap with asm68K (the same assembler used for
  the crt0/seam, runtime, and libm) and archives them into a single libheap.a.
  The malloc/free/realloc/calloc seam (Phase 5) links against it; archive member
  selection means programs that make no heap calls pull nothing.

  PROVENANCE: vendored 2026-07-21 from the worm68k heap library
    C:\git\worm68k\68kTools\libraries\heap   (31 library .a68 sources + heap.inc)
  The upstream test drivers (*Test.a68) and the harness stub.a68 are NOT part of
  the library and are excluded by tools/vendor-sync.ps1. Rebuild after syncing.
#>
[CmdletBinding()]
param(
  [string]$Asm    = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ar     = 'C:\git\osiris\toolchain\binutils\m68k-elf-ar.exe',
  [string]$Ranlib = 'C:\git\osiris\toolchain\binutils\m68k-elf-ranlib.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$heap = Join-Path $repo 'lib\heap'
$outA = Join-Path $heap 'libheap.a'

foreach ($t in @($Asm, $Ar, $Ranlib)) {
  if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "build-libheap: tool not found: $t" }
}

$objDir = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-libheap-obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
Get-ChildItem $objDir -Filter *.o -ErrorAction SilentlyContinue | Remove-Item -Force

# Assemble from the heap root so `include heap.inc` resolves via /I. .
$objs = @()
Push-Location $heap
try {
  foreach ($s in (Get-ChildItem -Filter *.a68)) {
    $o = Join-Path $objDir ($s.BaseName + '.o')
    & $Asm /Cx /elf /c /nologo /I. "/Fo$o" $s.FullName 2>&1 | Out-String -OutVariable out | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host $out; throw "build-libheap: asm failed: $($s.Name)" }
    $objs += $o
  }
}
finally { Pop-Location }

Remove-Item $outA -Force -ErrorAction SilentlyContinue
& $Ar rcs $outA @objs
if ($LASTEXITCODE -ne 0) { throw "build-libheap: ar failed" }
& $Ranlib $outA
if ($LASTEXITCODE -ne 0) { throw "build-libheap: ranlib failed" }

Write-Host ("build-libheap: {0}  ({1} bytes, {2} objects)" -f $outA, (Get-Item $outA).Length, $objs.Count) -ForegroundColor Green
$outA
