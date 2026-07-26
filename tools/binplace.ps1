#requires -version 5
<#
.SYNOPSIS
  Binplace the c68k toolchain into an in-repo output tree (out/) so a
  distribution package can be assembled from real files instead of %TEMP%.

.DESCRIPTION
  Produces three self-contained target trees under -OutDir (default: out/):

    out/cross/            HOST cross-compiler (runs on the PC/Windows)
      c68k.exe
      include/            full header set (builtin + hosted)
      lib/osiris/         Osiris link libraries   (compile with -target osiris)
      lib/cpm/            CP/M-68K link libraries  (compile with -target cpm)
    out/osiris/           NATIVE Osiris toolchain (runs under Osiris)
      CC.PRG              self-hosted compiler
      include/
      lib/                libc.a libm.a libheap.a rt68k.o osiris_sys.o osiris-prg.ld
    out/cpm/              NATIVE CP/M-68K toolchain (runs under CP/M-68K)
      CC.68K
      include/
      lib/                libc.a libm.a libheap.a rt68k.o cpm_sys.o cpm.o cpm68k.ld

  The header set is OS-neutral: the same files serve both targets, because
  -target only flips predefined macros (__osiris__ / __CPM68K__) -- it selects
  no different headers. The platform split lives entirely in lib/ (the crt0/
  seam object plus the linker script). The .a archives (libc/libm/libheap) and
  rt68k.o are identical across both OSes; only the seam + .ld differ.

  Duplicated headers (float.h/limits.h/stdbool.h/stdint.h exist under both
  include/ and libc/include/) are merged builtin-on-top, matching the compiler's
  own search order (-Iinclude before -Ilibc\include). The cross compiler then
  auto-finds every standard header via <argv0dir>/include -- no -I needed.

  out/ is a build artifact (gitignored). Rebuild any time after changing the
  compiler or the libraries. The native compilers are cross-built here (they do
  NOT require the simulator); linking still needs the external m68k-elf-ld /
  mkdri, which are prerequisites, not vendored.

.EXAMPLE
  pwsh tools/binplace.ps1
#>
[CmdletBinding()]
param(
  [string]$Cc       = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\c68k.exe'),
  [string]$OutDir   = (Join-Path (Split-Path $PSScriptRoot -Parent) 'out'),
  [string]$Asm      = (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\bin\asm68K.exe'),
  [string]$Ld       = (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\bin\m68k-elf-ld.exe'),
  [string]$Mkdri    = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\mkdri.exe',
  [string]$OsirisLd = 'C:\git\osiris\ld\osiris-prg.ld'
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path $PSScriptRoot -Parent
$toolsO = Join-Path $repo 'tools\osiris'
$toolsC = Join-Path $repo 'tools\cpm'
$cpmLd  = Join-Path $toolsC 'cpm68k.ld'

if (-not (Test-Path $Cc)) {
  throw "binplace: cross compiler not found at '$Cc'. Build it first:`n  cmake --build build --config Release`n(or pass -Cc <path\to\c68k.exe>)."
}
foreach ($t in @($Asm, $Ld, $Mkdri)) {
  if (-not (Test-Path $t)) { throw "binplace: missing external tool: $t" }
}
foreach ($f in @($OsirisLd, $cpmLd)) {
  if (-not (Test-Path $f)) { throw "binplace: missing linker script: $f" }
}

Write-Host "== c68k binplace -> $OutDir ==" -ForegroundColor Cyan

# --- fresh output tree ---
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
foreach ($d in @(
    'cross', 'cross\bin', 'cross\include', 'cross\lib\osiris', 'cross\lib\cpm',
    'osiris', 'osiris\include', 'osiris\lib',
    'cpm', 'cpm\include', 'cpm\lib')) {
  New-Item -ItemType Directory -Force -Path (Join-Path $OutDir $d) | Out-Null
}

# --- shared header set: hosted first, builtin on top (builtin wins on dups,
#     matching the -Iinclude -Ilibc\include search order the compiles use) ---
function Copy-Headers([string]$dest) {
  Copy-Item (Join-Path $repo 'libc\include\*') $dest -Recurse -Force
  Copy-Item (Join-Path $repo 'include\*')      $dest -Recurse -Force
  Get-ChildItem $dest -Recurse -Filter '.gitkeep' -Force | Remove-Item -Force -ErrorAction SilentlyContinue
}
Copy-Headers (Join-Path $OutDir 'cross\include')
Copy-Headers (Join-Path $OutDir 'osiris\include')
Copy-Headers (Join-Path $OutDir 'cpm\include')

# --- neutral archives shared by both targets (build on demand if missing) ---
$libm   = Join-Path $repo 'lib\libm\libm.a'
$libheap = Join-Path $repo 'lib\heap\libheap.a'
if (-not (Test-Path $libm))   { & (Join-Path $PSScriptRoot 'build-libm.ps1')   | Out-Null }
if (-not (Test-Path $libheap)) { & (Join-Path $PSScriptRoot 'build-libheap.ps1') | Out-Null }

# --- Osiris: native CC.PRG + link libraries -----------------------------------
# build-cc.ps1 also emits libc.a, osiris_sys.o and rt68k.o into its -OutDir.
$obuild = Join-Path ([IO.Path]::GetTempPath()) 'c68k-binplace-osiris'
Write-Host '-- Osiris: CC.PRG + libraries' -ForegroundColor Cyan
& (Join-Path $toolsO 'build-cc.ps1') -Cc $Cc -OutDir $obuild -Asm $Asm -Ld $Ld -LdScript $OsirisLd | Out-Null
$oLib = Join-Path $OutDir 'osiris\lib'
Copy-Item (Join-Path $obuild 'CC.PRG') (Join-Path $OutDir 'osiris\CC.PRG') -Force
foreach ($f in 'libc.a', 'osiris_sys.o', 'rt68k.o') { Copy-Item (Join-Path $obuild $f) $oLib -Force }
Copy-Item $libm    $oLib -Force
Copy-Item $libheap $oLib -Force
Copy-Item $OsirisLd (Join-Path $oLib 'osiris-prg.ld') -Force

# --- CP/M-68K: native CC.68K + link libraries ---------------------------------
# build-cc-68k.ps1 also emits libc.a, cpm.o, cpm_sys.o and rt68k.o into -OutDir.
$cbuild = Join-Path ([IO.Path]::GetTempPath()) 'c68k-binplace-cpm'
Write-Host '-- CP/M-68K: CC.68K + libraries' -ForegroundColor Cyan
& (Join-Path $toolsC 'build-cc-68k.ps1') -Cc $Cc -OutDir $cbuild -Asm $Asm -Ld $Ld -Mkdri $Mkdri | Out-Null
$cLib = Join-Path $OutDir 'cpm\lib'
Copy-Item (Join-Path $cbuild 'CC.68K') (Join-Path $OutDir 'cpm\CC.68K') -Force
foreach ($f in 'libc.a', 'cpm.o', 'cpm_sys.o', 'rt68k.o') { Copy-Item (Join-Path $cbuild $f) $cLib -Force }
Copy-Item $libm    $cLib -Force
Copy-Item $libheap $cLib -Force
Copy-Item $cpmLd (Join-Path $cLib 'cpm68k.ld') -Force

# --- cross: the host compiler + BOTH targets' libraries -----------------------
Copy-Item $Cc (Join-Path $OutDir 'cross\c68k.exe') -Force
# vendored assembler + linker (tools/bin) so the packaged cross tree is
# self-contained (c68k finds asm68K via PATH / C68K_AS; link uses m68k-elf-ld).
Copy-Item (Join-Path $repo 'tools\bin\*') (Join-Path $OutDir 'cross\bin') -Force
Copy-Item (Join-Path $oLib '*') (Join-Path $OutDir 'cross\lib\osiris') -Force
Copy-Item (Join-Path $cLib '*') (Join-Path $OutDir 'cross\lib\cpm')    -Force

# --- README / manifest --------------------------------------------------------
$ver = (& $Cc --version) 2>&1
@"
c68k toolchain -- binplaced $(Get-Date -Format 'yyyy-MM-dd HH:mm')
$ver

Three self-contained trees:

  cross/    Host cross-compiler (runs on the PC). Emits OS-neutral m68k ELF
            objects; -target osiris|cpm only selects predefined macros.
    c68k.exe         run it; it auto-finds headers in ./include
    include/         full C header set (builtin + hosted), OS-neutral
    lib/osiris/      Osiris link inputs   (seam + crt0 + archives + .ld)
    lib/cpm/         CP/M-68K link inputs (seam + crt0 + archives + .ld)

  osiris/   Native Osiris compiler (runs UNDER Osiris).
    CC.PRG           the self-hosted compiler
    include/         same headers (copy onto the target disk)
    lib/             libc.a libm.a libheap.a rt68k.o osiris_sys.o osiris-prg.ld

  cpm/      Native CP/M-68K compiler (runs UNDER CP/M-68K).
    CC.68K           the self-hosted compiler (DRI contiguous transient)
    include/         same headers
    lib/             libc.a libm.a libheap.a rt68k.o cpm_sys.o cpm.o cpm68k.ld

The headers do NOT differ across platforms -- one set serves both. The platform
split lives only in lib/ (the crt0/seam object + linker script).

Cross-compile + link (needs the external m68k-elf-ld / mkdri):

  Osiris .PRG:
    cross\c68k.exe -target osiris -fintegrated-as -c prog.c -o prog.o
    m68k-elf-ld -pie --no-dynamic-linker -z max-page-size=0x20 -s \
      -T cross\lib\osiris\osiris-prg.ld \
      cross\lib\osiris\osiris_sys.o prog.o -Lcross\lib\osiris -lc \
      cross\lib\osiris\rt68k.o cross\lib\osiris\libm.a cross\lib\osiris\libheap.a \
      -o PROG.PRG

  CP/M-68K .68K:
    cross\c68k.exe -target cpm -fintegrated-as -c prog.c -o prog.o
    m68k-elf-ld -T cross\lib\cpm\cpm68k.ld -Ttext 0x500 \
      cross\lib\cpm\cpm_sys.o prog.o cross\lib\cpm\cpm.o -Lcross\lib\cpm -lc \
      cross\lib\cpm\rt68k.o cross\lib\cpm\libm.a cross\lib\cpm\libheap.a -o PROG.elf
    mkdri -b500 -y -o PROG.68K PROG.elf

Based on chibicc (MIT). This tree is a build artifact (regenerate with
tools/binplace.ps1); it is not committed.
"@ | Set-Content (Join-Path $OutDir 'README.txt') -Encoding ASCII

# --- summary ------------------------------------------------------------------
Write-Host ''
Write-Host '== binplaced ==' -ForegroundColor Green
foreach ($item in @(
    @{ n = 'cross/c68k.exe'; p = 'cross\c68k.exe' },
    @{ n = 'osiris/CC.PRG';  p = 'osiris\CC.PRG' },
    @{ n = 'cpm/CC.68K';     p = 'cpm\CC.68K' })) {
  $fp = Join-Path $OutDir $item.p
  if (Test-Path $fp) {
    Write-Host ("  {0,-16} {1,10:n0} bytes" -f $item.n, (Get-Item $fp).Length)
  }
}
$total = (Get-ChildItem $OutDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("  {0,-16} {1,10:n0} bytes total, {2} files" -f $OutDir, $total, (Get-ChildItem $OutDir -Recurse -File).Count)
$OutDir
