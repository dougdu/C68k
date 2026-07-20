#requires -version 5
<#
.SYNOPSIS
  Demonstrate (and smoke-test) c68k source-level debug info: build a sample
  with -g and show the DWARF driving m68k-elf tools on the linked Osiris .PRG.

.DESCRIPTION
  c68k -g makes the integrated assembler emit STT_FUNC symbols and DWARF
  .debug_info / .debug_line / .debug_abbrev (with relocations, so the info
  survives linking). This script builds samples/<name>.c with C68K_G=1 (which
  keeps the symbols/debug sections in the .PRG) and exercises readelf, objdump
  -dl, addr2line and gdb against it. Exit 0 iff gdb resolves a source line to an
  address (proving the DWARF is well-formed end to end).

.EXAMPLE
  pwsh tools/debug-demo.ps1 -Sample hexdump -Line 88
#>
[CmdletBinding()]
param(
  [string]$Sample = 'hexdump',
  [int]$Line = 88,
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Binutils = 'C:\git\osiris\toolchain\binutils'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $repo "samples\$Sample.c"
if (-not (Test-Path $src)) { throw "debug-demo: no such sample '$src'" }

$readelf  = Join-Path $Binutils 'm68k-elf-readelf.exe'
$objdump  = Join-Path $Binutils 'm68k-elf-objdump.exe'
$gdb      = Join-Path $Binutils 'm68k-elf-gdb.exe'
foreach ($t in @($readelf, $objdump, $gdb)) {
  if (-not (Test-Path $t)) { throw "debug-demo: missing tool '$t'" }
}

# Build with debug info (C68K_G keeps symbols/debug in the linked .PRG).
if (-not $env:C68K_INTEGRATED_AS) { $env:C68K_INTEGRATED_AS = '1' }
$env:C68K_G = '1'
$od  = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-debug-demo'
$name = $Sample.ToUpper(); if ($name.Length -gt 8) { $name = $name.Substring(0, 8) }
$prg = @(& (Join-Path $repo 'tools\osiris\build-prg.ps1') -Src $src -Name $name -Cc $Cc -OutDir $od 2>&1 |
         Where-Object { "$_" -like '*.PRG' })[-1]
$env:C68K_G = $null
if (-not $prg -or -not (Test-Path $prg)) { throw "debug-demo: build produced no .PRG" }
Write-Host "debug-demo: built $prg with -g" -ForegroundColor Cyan

Write-Host "`n== .debug_info compilation unit ==" -ForegroundColor Yellow
& $readelf --debug-dump=info $prg 2>&1 | Select-Object -First 12

Write-Host "`n== source-interleaved disassembly (objdump -dl, _main) ==" -ForegroundColor Yellow
$dl = & $objdump -dl $prg 2>&1
$mi = ($dl | Select-String '<_main>:').LineNumber
if ($mi) { $dl[($mi - 1)..([Math]::Min($dl.Count - 1, $mi + 14))] | Write-Host }

Write-Host "`n== gdb: map $($Sample).c:$Line to an address, then list source ==" -ForegroundColor Yellow
$info = & $gdb --batch -nx -ex "info line $Sample.c:$Line" -ex "list $Sample.c:$Line" $prg 2>&1
$info | Select-Object -First 16 | Write-Host

$ok = ($info -join "`n") -match "Line $Line .*starts at address 0x[0-9a-f]+"
Write-Host ("`ndebug-demo: gdb line->address mapping {0}" -f ($(if ($ok) { 'OK' } else { 'FAILED' }))) `
  -ForegroundColor ($(if ($ok) { 'Green' } else { 'Red' }))
exit ($(if ($ok) { 0 } else { 1 }))
