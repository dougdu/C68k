#requires -version 5
<#
.SYNOPSIS
  Lockstep test runner: compile each sample for BOTH targets, run it on Osiris
  (.PRG) and CP/M-68K (.68K), and require the same expected output on both.

.DESCRIPTION
  This is the dual-target guarantee gate: one C source, one golden expectation,
  and it must hold on both OSes. Each case boots the project-local sim twice
  (once per OS). Exit 0 = every case passed on both OSes.

.EXAMPLE
  pwsh tools/run-lockstep.ps1
#>
[CmdletBinding()]
param(
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [ValidateSet('16mb','1mb')][string]$Model = '16mb'
)
$ErrorActionPreference = 'Continue'
$repo = Split-Path $PSScriptRoot -Parent

# name (<=8, shared by .PRG and .68K) + source + the golden substrings.
$cases = @(
  @{ Run = 'HELLO';  Src = 'samples\hello.c';      Expect = @('Hello, Osiris! -- c68k') }
  @{ Run = 'FILERW'; Src = 'samples\filerw.c';     Expect = @('c68k file I/O works') }
  @{ Run = 'PRINTF'; Src = 'samples\printftest.c'; Expect = @('int=42 hex=ff str=abc char=Z',
                                                              'pad=[   42] zero=[0042] left=[42   ]',
                                                              'll=1000000000000 u=4000000000') }
  @{ Run = 'CORETEST'; Src = 'tests\lockstep\coretest.c'; Expect = @('SUITE PASS') }
  @{ Run = 'C99TEST';  Src = 'tests\lockstep\c99test.c';  Expect = @('C99 PASS') }
  @{ Run = 'MATHTEST'; Src = 'tests\lockstep\mathtest.c'; Expect = @('MATH PASS') }
  @{ Run = 'LIBTEST';  Src = 'tests\lockstep\libtest.c';  Expect = @('LIB PASS') }
  @{ Run = 'MEMTEST';  Src = 'tests\lockstep\memtest.c';  Expect = @('MEM PASS 15/15') }
  @{ Run = 'TIMETEST'; Src = 'tests\lockstep\timetest.c'; Expect = @('TIME PASS') }
  @{ Run = 'SETJMP';   Src = 'tests\lockstep\setjmp.c';   Expect = @('SETJMP PASS 6/6') }
  @{ Run = 'IOTEST';   Src = 'tests\lockstep\iotest.c';   Expect = @('IOTEST PASS 44/44') }
  @{ Run = 'FDTEST';   Src = 'tests\lockstep\fdtest.c';   Expect = @('FDTEST PASS 18/18') }
  @{ Run = 'STATDIR';  Src = 'tests\lockstep\statdir.c';  Expect = @('STATDIR PASS 20/20') }
  @{ Run = 'ENVTEST';  Src = 'tests\lockstep\envtest.c';  Expect = @('ENVTEST PASS 11/11') }
  @{ Run = 'CONIOT';   Src = 'tests\lockstep\coniot.c';   Expect = @('CONIOT PASS 3/3') }
  @{ Run = 'POLISH';   Src = 'tests\lockstep\polish.c';   Expect = @('POLISH PASS 13/13') }
  @{ Run = 'CONVTEST'; Src = 'tests\lockstep\convtest.c'; Expect = @('CONVTEST PASS 26/26') }
  @{ Run = 'C11TEST';  Src = 'tests\lockstep\c11test.c';  Expect = @('C11TEST PASS 18/18') }
  @{ Run = 'WCHART';   Src = 'tests\lockstep\wchart.c';   Expect = @('WCHART PASS 32/32') }
  @{ Run = 'TZTEST';   Src = 'tests\lockstep\tztime.c';   Expect = @('TZTEST PASS 10/10') }
  @{ Run = 'HEXDUMP';  Src = 'samples\hexdump.c';         Expect = @('hexdump of HEXTEST.BIN',
                                                                     '|c68k hexdump OK.|',
                                                                     '63 36 38 6b', '128 bytes') }
)

$osRun  = Join-Path $repo 'tools\osiris\run-osiris.ps1'
$cpmRun = Join-Path $repo 'tools\cpm\run-cpm.ps1'

$pass = 0; $fail = 0
foreach ($c in $cases) {
  $src = Join-Path $repo $c.Src
  Write-Host ("== {0} ({1}) ==" -f $c.Run, $c.Src) -ForegroundColor Cyan

  & $osRun -Src $src -Run $c.Run -Expect $c.Expect -Cc $Cc *> $null
  $osOk = ($LASTEXITCODE -eq 0)
  Write-Host ("   Osiris  : {0}" -f ($(if ($osOk) { 'PASS' } else { 'FAIL' }))) -ForegroundColor ($(if ($osOk) { 'Green' } else { 'Red' }))

  & $cpmRun -Src $src -Run $c.Run -Expect $c.Expect -Cc $Cc -Model $Model *> $null
  $cpmOk = ($LASTEXITCODE -eq 0)
  Write-Host ("   CP/M-68K: {0}" -f ($(if ($cpmOk) { 'PASS' } else { 'FAIL' }))) -ForegroundColor ($(if ($cpmOk) { 'Green' } else { 'Red' }))

  if ($osOk -and $cpmOk) { $pass++ } else { $fail++ }
}

Write-Host ("=== lockstep: {0}/{1} cases pass on BOTH OSes ===" -f $pass, ($pass + $fail)) `
  -ForegroundColor ($(if ($fail -eq 0) { 'Green' } else { 'Red' }))
exit $fail
