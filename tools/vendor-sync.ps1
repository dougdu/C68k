#requires -version 5
<#
.SYNOPSIS
  Re-sync the vendored third-party m68k libraries (libm, libheap) from their
  upstream worm68k source trees into this repo, and record a drift manifest.

.DESCRIPTION
  libm and libheap are vendored as SOURCE (not prebuilt .a) so both the GNU
  cross toolchain and the native Osiris LIB/LINK can build their own archive
  from one source of truth (see docs/libc-archive-design.md sections 8-9).

  For each library this:
    * removes the previously vendored .a68/.inc (so upstream deletions drop out),
    * copies the current upstream sources into the in-tree vendored directory,
    * prunes any build artifacts (*.o, *.lst) that rode along, and
    * writes VENDOR.txt: upstream path, sync timestamp, and a per-file SHA-256
      so `git diff lib/**/VENDOR.txt` shows exactly what changed on each sync.

  After syncing, rebuild the archives and re-run the dual-target suite:
      pwsh tools/build-libm.ps1
      pwsh tools/build-libheap.ps1
      pwsh tools/run-lockstep.ps1

  Upstream paths are parameters; override them if your worm68k checkout differs.

.EXAMPLE
  pwsh tools/vendor-sync.ps1
#>
[CmdletBinding()]
param(
  [string]$Libm = 'C:\git\worm68k\68kTools\libraries\float\ieee754',
  [string]$Heap = 'C:\git\worm68k\68kTools\libraries\heap'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

function Sync-Vendor {
  param(
    [string]$Name,
    [string]$Src,
    [string]$Dst,
    [scriptblock]$SelectSrc   # given $Src, returns the FileInfo[] to vendor
  )
  if (-not (Test-Path $Src)) { throw "vendor-sync: upstream not found for ${Name}: $Src" }
  New-Item -ItemType Directory -Force -Path $Dst | Out-Null

  # Drop the old vendored sources so removed-upstream files disappear here too.
  Get-ChildItem $Dst -Recurse -Include *.a68, *.inc -ErrorAction SilentlyContinue | Remove-Item -Force

  foreach ($f in (& $SelectSrc $Src)) {
    $rel    = $f.FullName.Substring($Src.Length).TrimStart('\')
    $target = Join-Path $Dst $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item $f.FullName $target -Force
  }

  # Prune stray build artifacts and confirm no test drivers slipped in.
  Get-ChildItem $Dst -Recurse -Include *.o, *.lst -ErrorAction SilentlyContinue | Remove-Item -Force

  $vendored = Get-ChildItem $Dst -Recurse -Include *.a68, *.inc | Sort-Object FullName
  $hashes   = $vendored | Get-FileHash -Algorithm SHA256
  $manifest = @(
    "vendored library : $Name"
    "upstream source  : $Src"
    "synced (UTC)     : $((Get-Date).ToUniversalTime().ToString('u'))"
    "file count       : $($vendored.Count)"
    ''
    'files (sha256, path):'
  ) + ($hashes | ForEach-Object {
      '  {0}  {1}' -f $_.Hash.Substring(0, 16), $_.Path.Substring($Dst.Length).TrimStart('\')
    })
  Set-Content -Path (Join-Path $Dst 'VENDOR.txt') -Value $manifest -Encoding ASCII

  Write-Host ("vendor-sync: {0,-8} <- {1}  ({2} files)" -f $Name, $Src, $vendored.Count) -ForegroundColor Green
}

# --- libm: the ieee754 core/conv/fmt/math .a68 + ieee754*.inc ---
Sync-Vendor 'libm' $Libm (Join-Path $repo 'lib\libm') {
  param($s)
  @(Get-ChildItem -Path (@('core', 'conv', 'fmt', 'math') | ForEach-Object { Join-Path $s $_ }) -Filter *.a68 -Recurse) +
  @(Get-ChildItem -Path $s -Filter *.inc)
}

# --- libheap: the library .a68 (Heap*/GetMachine*, minus *Test) + heap.inc.
#     Excludes the upstream test drivers (*Test.a68) and the harness stub.a68. ---
Sync-Vendor 'libheap' $Heap (Join-Path $repo 'lib\heap') {
  param($s)
  @(Get-ChildItem -Path $s -Filter *.a68 |
      Where-Object { ($_.Name -like 'Heap*' -or $_.Name -like 'GetMachine*') -and $_.Name -notlike '*Test.a68' }) +
  @(Get-ChildItem -Path $s -Filter heap.inc)
}

Write-Host 'vendor-sync: done. Rebuild: tools/build-libm.ps1 + tools/build-libheap.ps1, then tools/run-lockstep.ps1' -ForegroundColor Cyan
