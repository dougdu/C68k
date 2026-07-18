#requires -version 5
<#
.SYNOPSIS
  c68k P2 bare-metal test runner (Windows host).

.DESCRIPTION
  For every tests/m68k/*.c file (each carrying a `// c68k-expect: N` header),
  this:
    1. compiles + assembles it with `c68k -c` (exercises the real driver ->
       asm68K path, producing an ELF object),
    2. links it with the test crt0 and the 68000 runtime helpers via
       m68k-elf-ld at -Ttext 0x1000, entry _start,
    3. runs the image under `sim68k --gdb`, breakpoints at _done, and reads
       the return value out of D0,
    4. compares D0 against the expected value.

  Toolchain locations default to the standard worm68k / SysGCC install paths
  and can be overridden with the parameters below. sim68k, asm68K and the
  m68k-elf toolchain are Windows-only here, so this harness is not run in CI.

.EXAMPLE
  pwsh tools/m68k/run-tests.ps1 -Cc build\c68k.exe
#>
param(
  [string]$Cc   = 'c68k.exe',
  [string]$Asm  = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Ld   = 'C:\SysGCC\m68k-elf\bin\m68k-elf-ld.exe',
  [string]$Sim  = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\sim68k.exe',
  [string]$Gdb  = 'C:\SysGCC\m68k-elf\bin\m68k-elf-gdb.exe',
  [string]$FloatLib = 'C:\git\worm68k\68kTools\libraries\float\ieee754\libieee754d.a',
  [int]$Port    = 1234
)

$ErrorActionPreference = 'Continue'

# tools/m68k -> repo root
$repo    = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testdir = Join-Path $repo 'tests\m68k'
$inc     = Join-Path $repo 'libc\include'
$rtSrc   = Join-Path $repo 'lib\runtime\rt68k.a68'
$crtSrc  = Join-Path $PSScriptRoot 'crt0-test.a68'
$work    = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-m68k-tests'
New-Item -ItemType Directory -Force -Path $work | Out-Null

# Resolve the c68k binary: honor -Cc, else probe the usual build locations.
if (-not (Get-Command $Cc -ErrorAction SilentlyContinue)) {
  foreach ($cand in @((Join-Path $repo 'build\c68k.exe'),
                      (Join-Path $repo 'c68k.exe'),
                      (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'))) {
    if (Test-Path $cand) { $Cc = $cand; break }
  }
}
foreach ($tool in @($Cc, $Asm, $Ld, $Sim, $Gdb)) {
  if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: tool not found: $tool" -ForegroundColor Red
    exit 2
  }
}

Set-Location $work

# Assemble the fixed pieces (crt0 + runtime) once.
& $Asm /Cx /elf /c /nologo /Focrt0.o  $crtSrc *> crt.log
if ($LASTEXITCODE -ne 0) { Write-Host 'crt0 assemble FAILED'; Get-Content crt.log -TotalCount 8; exit 1 }
& $Asm /Cx /elf /c /nologo /Fort68k.o $rtSrc  *> rt.log
if ($LASTEXITCODE -ne 0) { Write-Host 'runtime assemble FAILED'; Get-Content rt.log -TotalCount 8; exit 1 }

$gcmds = @('set pagination off',
           "target remote 127.0.0.1:$Port",
           'load',
           'set $pc = _start',
           'break _done',
           'continue',
           'printf "RESULT=%d\n", $d0',
           'kill',
           'quit')
Set-Content cmds.gdb -Encoding ascii -Value $gcmds

$pass = 0; $fail = 0
foreach ($tc in Get-ChildItem $testdir -Filter *.c | Sort-Object Name) {
  $name = $tc.BaseName
  $hdr  = (Get-Content $tc.FullName -TotalCount 3) -join "`n"
  $mx   = [regex]::Match($hdr, 'c68k-expect:\s*(-?\d+)')
  if (-not $mx.Success) { Write-Host "[$name] SKIP (no c68k-expect)"; continue }
  $expect = [int]$mx.Groups[1].Value

  & $Cc -ffreestanding -c $tc.FullName -o case.o "-I$inc" *> cc.log
  if ($LASTEXITCODE -ne 0) { Write-Host "[$name] c68k FAIL"; Get-Content cc.log -TotalCount 8; $fail++; continue }
  & $Ld -Ttext 0x1000 -e _start -o case.elf crt0.o case.o rt68k.o $FloatLib *> ld.log
  if ($LASTEXITCODE -ne 0) { Write-Host "[$name] ld FAIL"; Get-Content ld.log -TotalCount 8; $fail++; continue }

  Get-Process sim68k -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 250
  Start-Process $Sim -ArgumentList '--gdb', "$Port", '--acia-port', 'none' `
                -WorkingDirectory $work -WindowStyle Hidden
  Start-Sleep -Milliseconds 800
  $out = & $Gdb -nx -batch -x cmds.gdb case.elf 2>&1 | Out-String
  Get-Process sim68k -ErrorAction SilentlyContinue | Stop-Process -Force

  $mr = [regex]::Match($out, 'RESULT=(-?\d+)')
  if (-not $mr.Success) { Write-Host "[$name] NO RESULT from sim68k"; $fail++; continue }
  $got = [int]$mr.Groups[1].Value
  if ($got -eq $expect) { Write-Host "[$name] OK ($got)" -ForegroundColor Green; $pass++ }
  else { Write-Host "[$name] MISMATCH got=$got expected=$expect" -ForegroundColor Red; $fail++ }
}

Write-Host ("=== c68k m68k tests: pass={0} fail={1} ===" -f $pass, $fail)
exit $fail
