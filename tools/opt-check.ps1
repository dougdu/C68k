#requires -version 5
<#
.SYNOPSIS
  Optimizer micro-checks: compile tiny standalone functions and assert the
  emitted asm contains (or, with -Negate, lacks) the instruction form a given
  optimizer phase produces. One rule per opportunity (docs/optimization-plan.md
  #12 catalog), so a transform's presence/absence is gated precisely.

.DESCRIPTION
  Each rule = { function source, -O level, a regex over the emitted asm, and
  whether the pattern must appear or be absent }. Rules marked Expect='pass'
  are the transforms that already fire (the harness self-test) and MUST hold;
  a failure exits 1. Rules marked Expect='pending' are future-phase targets:
  they report PENDING until their phase lands, then LANDED (flip them to 'pass'
  and check the box in optimization-plan.md).

.EXAMPLE
  pwsh tools/opt-check.ps1
#>
[CmdletBinding()]
param([string]$Cc = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\c68k.exe'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Cc)) { throw "opt-check: compiler not found: $Cc" }
$work = Join-Path ([IO.Path]::GetTempPath()) 'c68k-optcheck'
New-Item -ItemType Directory -Force -Path $work | Out-Null

# name | O | code | pattern | negate | expect | phase
$rules = @(
  # --- self-test: transforms that already fire at -O1 (MUST pass) ---
  @{ N='mul8->asl';       O=1; C='int f(int x){return x*8;}';                    P='asl\.l #3,d0';        Neg=$false; E='pass';    Ph='-O1' }
  @{ N='add5->addq';      O=1; C='int f(int x){return x+5;}';                    P='addq\.l #5,d0';       Neg=$false; E='pass';    Ph='-O1' }
  @{ N='lt10->cmpi';      O=1; C='int f(int x){return x<10;}';                   P='cmp\.l #10,d0';       Neg=$false; E='pass';    Ph='-O1' }
  @{ N='udiv4->lsr';      O=1; C='unsigned f(unsigned x){return x/4;}';          P='lsr\.l #2,d0';        Neg=$false; E='pass';    Ph='-O1' }
  # --- OP1 (Tier A) ---
  @{ N='no-bra-to-next';  O=1; C='int f(int x){return x;}';                      P='bra L_return';        Neg=$true;  E='pass';    Ph='OP1 #1' }
  # --- OP2 (Tier B) ---
  @{ N='mem-operand';     O=2; C='int f(int a,int b){return a+b;}';              P='add\.l \d+\(a6\),d0'; Neg=$false; E='pass';    Ph='OP2 #4' }
  @{ N='direct-store';    O=2; C='int g; void f(int v){g=v;}';                   P='move\.l d0,_g';       Neg=$false; E='pass';    Ph='OP2 #5' }
  @{ N='const-left-cmp';  O=2; C='int f(int x){return x>10;}';                   P='cmp\.l #10,d0';       Neg=$false; E='pass';    Ph='OP2 #6' }
  @{ N='indexed-addr';    O=2; C='int a[9]; int f(int i){return a[i];}';         P='\(a0,d\d\.l\)';       Neg=$false; E='pass';    Ph='OP2 #7' }
  @{ N='sdiv4-no-call';   O=2; C='int f(int x){return x/4;}';                    P='__divsi3';            Neg=$true;  E='pass';    Ph='OP2 #8' }
  # --- OP3 (Tier C) ---
  @{ N='cond-no-scc';     O=2; C='int f(int a,int b){if(a<b)return 1;return 0;}';P='slt d0';              Neg=$true;  E='pass';    Ph='OP3 #9' }
)

$fail = 0; $landed = 0
Write-Host ''
Write-Host 'c68k optimizer micro-checks' -ForegroundColor Cyan
Write-Host ('{0,-16} {1,-5} {2,-9} {3}' -f 'check', 'lvl', 'phase', 'status')
Write-Host ('-' * 52)
foreach ($r in $rules) {
  $src = Join-Path $work ($r.N.Replace('->','_').Replace(' ','_') + '.c')
  Set-Content -Path $src -Value $r.C
  $asm = & $Cc -S "-O$($r.O)" $src -o - 2>$null
  if ($LASTEXITCODE -ne 0) { $asm = ''; }
  $text = ($asm -join "`n")
  $matched = [bool]([regex]::IsMatch($text, $r.P))
  $holds = if ($r.Neg) { -not $matched } else { $matched }   # transform present?
  switch ($r.E) {
    'pass' {
      if ($holds) { $st = 'PASS';    $col = 'Green' }
      else        { $st = 'FAIL';    $col = 'Red';  $fail++ }
    }
    'pending' {
      if ($holds) { $st = 'LANDED';  $col = 'Yellow'; $landed++ }
      else        { $st = 'PENDING'; $col = 'DarkGray' }
    }
  }
  Write-Host ('{0,-16} {1,-5} {2,-9} ' -f $r.N, "-O$($r.O)", $r.Ph) -NoNewline
  Write-Host $st -ForegroundColor $col
}
Write-Host ('-' * 52)
if ($landed) { Write-Host "$landed pending check(s) now LANDED -- flip to Expect='pass' + check the box in optimization-plan.md." -ForegroundColor Yellow }
if ($fail)   { Write-Host "$fail self-test check(s) FAILED." -ForegroundColor Red; exit 1 }
Write-Host 'opt-check: OK (all self-tests pass)' -ForegroundColor Green
exit 0
