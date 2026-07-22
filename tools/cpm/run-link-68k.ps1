#requires -version 5
<#
.SYNOPSIS
  Link a c68k C program NATIVELY on CP/M-68K with LINK.68K, consuming MULTIPLE
  archives (libc.a + libm.a + libheap.a), emit a runnable DRI .68K via LINK's
  CP/M output mode, run it on the 16 MB CP/M model, and check its output.

.DESCRIPTION
  Exercises the native-linker enhancements that needed the large-memory image:
    * MULTIPLE input archives per link (lk_main fixpoint symbol sweep over
      lm_arctxs[]) -- a member pulled from one archive can satisfy an undefined
      referenced from another;
    * the grow-on-demand internal tables (pulled list, archive contexts, object
      tables via lk_grow) -- a full libc/libm/libheap link pulls dozens of
      members and would overflow the old fixed caps.
  Both OOM'd or overran on the 1 MB model; the 16 MB image (cpu 68000) gives
  LINK room to actually run them.

  Pipeline (the native CP/M LINK replaces the host m68k-elf-ld step that
  tools/cpm/build-68k.ps1 normally uses):

      c68k -c prog.c -> PROG.O ;  c68k -c cpm.c -> CPM.O
      asm68K cpm_sys.a68 -> CPM_SYS.O ;  asm68K rt68k.a68 -> RT68K.O
      build-libc.ps1 -> LIBC.A ;  lib/libm/libm.a ;  lib/heap/libheap.a
      LINK /O:PROG.68K CPM_SYS.O PROG.O CPM.O RT68K.O LIBC.A LIBM.A LIBHEAP.A
      PROG                                                    (run it)

  Object/archive production is delegated to build-68k.ps1 (same objects it
  feeds to ld); this harness only swaps in the native LINK and runs the result.

  Everything is staged on a scratch D: (scsi1) with cpmcp; the CP/M shell is
  driven over the sim's TCP ACIA console (poll the D> prompt to detect when the
  link and the run finish). Drive map: A: = boot floppy, B: = unmounted (DO NOT
  touch), C: = scsi0 (system), D: = scsi1 (deploy target).

  -Model selects the CBIOS platform: '16mb' (cpu 68000, default) is required for
  the big multi-archive links; '1mb' (cpu 68008) is kept for comparison.

.EXAMPLE
  pwsh tools/cpm/run-link-68k.ps1 -Src tests/lockstep/memtest.c -Expect 'MEM PASS 15/15'
.EXAMPLE
  pwsh tools/cpm/run-link-68k.ps1 -Src samples/printftest.c -Expect 'int=42 hex=ff str=abc char=Z'
#>
[CmdletBinding()]
param(
  [string]$Src = 'tests\lockstep\memtest.c',
  [string]$Run = '',
  [string[]]$Expect = @('MEM PASS 15/15'),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Link68 = 'C:\git\worm68k\cpm68k\link\LINK.68K',
  [ValidateSet('16mb','1mb')][string]$Model = '16mb',
  [int]$BootWait = 9,
  [int]$MaxLinkWait = 600,
  [int]$MaxRunWait = 90,
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'
if (-not [IO.Path]::IsPathRooted($Src)) { $Src = Join-Path $repo $Src }
$Src = (Resolve-Path $Src).Path
if (-not $Run) { $Run = [IO.Path]::GetFileNameWithoutExtension($Src).ToUpper() }
if ($Run.Length -gt 8) { $Run = $Run.Substring(0,8) }

function Stop-AllSim {
  param([int]$SettleMs = 900, [int]$TimeoutMs = 6000)
  $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
  while ((Get-Date) -lt $deadline) {
    $ps = Get-Process c68k-sim68k -ErrorAction SilentlyContinue
    if (-not $ps) { break }
    $ps | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
  }
  Start-Sleep -Milliseconds $SettleMs
}

if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) { & (Join-Path $repo 'tools\bootstrap-simenv.ps1') }
$sim   = Join-Path $simenv 'c68k-sim68k.exe'
$rom   = Join-Path $simenv 'bootrom.bin'
$cpu   = if ($Model -eq '1mb') { '68008' } else { '68000' }
$flop  = Join-Path $simenv "cpmboot-$Model-144.img"
$scsi0 = Join-Path $simenv 'scsi0.img'
$scsi1 = Join-Path $simenv 'scsi1.img'
$cpmrm = Join-Path $simenv 'cpmrm.exe'
$cpmcp = Join-Path $simenv 'cpmcp.exe'
$fmt   = 'worm68k-8m'
$libmA  = Join-Path $repo 'lib\libm\libm.a'
$heapA  = Join-Path $repo 'lib\heap\libheap.a'
foreach ($p in @($sim,$rom,$flop,$scsi0,$scsi1,$cpmrm,$cpmcp,$Link68,$Cc,$libmA,$heapA)) {
  if (-not (Test-Path $p)) { throw "run-link-68k: missing asset '$p'" }
}

# ---- produce the objects + libc.a exactly as the ld path does, then swap in
#      the native LINK.  C68K_INTEGRATED_AS=1 -> c68k emits ELF objects directly
#      (what LINK consumes).  build-68k.ps1 also emits a host-ld .68K we ignore. ----
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cpm-link-run'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$env:C68K_INTEGRATED_AS = '1'
Write-Host ("build objects: {0}" -f (Split-Path $Src -Leaf)) -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'build-68k.ps1') -Src $Src -Name $Run -Cc $Cc -OutDir $work *>&1 |
  Where-Object { $_ -notmatch '\.68K$' } | Out-Null
$sysO  = Join-Path $work 'cpm_sys.o'
$rtO   = Join-Path $work 'rt68k.o'
$seamO = Join-Path $work 'cpm.o'
$libcA = Join-Path $work 'libc.a'
$progO = Join-Path $work "$Run.o"
foreach ($o in @($sysO,$rtO,$seamO,$libcA,$progO)) {
  if (-not (Test-Path $o)) { throw "run-link-68k: build-68k did not produce '$o'" }
}

Stop-AllSim
$bootImg = Join-Path $work 'cpmboot.img'
$dataImg = Join-Path $work 'scsi1.img'
$log     = Join-Path $work 'con.log'
Copy-Item $flop  $bootImg -Force
Copy-Item $scsi1 $dataImg -Force
Remove-Item $log -ErrorAction SilentlyContinue

# ---- deploy LINK + objects + the THREE archives onto D: (user 0) ----
$out68 = "$Run.68K"
$deploy = @(
  @{ Host = $Link68; Cpm = 'LINK.68K'   }
  @{ Host = $sysO;   Cpm = 'CPM_SYS.O'  }
  @{ Host = $progO;  Cpm = "$Run.O"     }
  @{ Host = $seamO;  Cpm = 'CPM.O'      }
  @{ Host = $rtO;    Cpm = 'RT68K.O'    }
  @{ Host = $libcA;  Cpm = 'LIBC.A'     }
  @{ Host = $libmA;  Cpm = 'LIBM.A'     }
  @{ Host = $heapA;  Cpm = 'LIBHEAP.A'  }
)
foreach ($d in $deploy) {
  & $cpmrm -f $fmt $dataImg "0:$($d.Cpm)" 2>&1 | Out-Null
  & $cpmcp -f $fmt $dataImg $d.Host "0:$($d.Cpm)" 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "cpmcp failed to deploy $($d.Cpm)" }
}
& $cpmrm -f $fmt $dataImg "0:$out68" 2>&1 | Out-Null   # clear any stale output

# ---- boot CP/M, drive the shell over the TCP ACIA console ----
$port = 8000 + (Get-Random -Minimum 0 -Maximum 1500)
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $sim
foreach ($a in @('--cpu',$cpu,'--mem','MAX',"--rom:$rom",'--acia-port','none','--acia-cts','tied','--acia-tcp-port',"$port",'--fdc-threads','on','--fd0',$bootImg,'--scsi0',$scsi0,'--scsi1',$dataImg)) { [void]$psi.ArgumentList.Add($a) }
$psi.WorkingDirectory = $simenv
$psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
$p = New-Object System.Diagnostics.Process; $p.StartInfo = $psi
[void]$p.Start()

$sb = New-Object System.Text.StringBuilder
$client = $null; $ns = $null; $rbuf = New-Object byte[] 8192
$linkCmd = "LINK /O:$out68 CPM_SYS.O $Run.O CPM.O RT68K.O LIBC.A LIBM.A LIBHEAP.A"
$rc = 1
try {
  for ($i = 0; $i -lt 60; $i++) { try { $client = New-Object System.Net.Sockets.TcpClient; $client.Connect('127.0.0.1', $port); break } catch { Start-Sleep -Milliseconds 250; $client = $null } }
  if ($null -eq $client) { throw "could not connect to sim ACIA on tcp/$port" }
  $ns = $client.GetStream()
  function _drain($secs) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { if ($ns.DataAvailable) { $n = $ns.Read($rbuf, 0, $rbuf.Length); if ($n -gt 0) { [void]$sb.Append([Text.Encoding]::ASCII.GetString($rbuf, 0, $n)) } } else { Start-Sleep -Milliseconds 80 } } }
  function _wr($s) { $b = [Text.Encoding]::ASCII.GetBytes($s); $ns.Write($b, 0, $b.Length); $ns.Flush() }

  # boot to A>
  $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt ($BootWait + 35) -and ($sb.ToString() -notmatch 'A>')) { _drain 1 }
  _wr "D:`r"
  $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt 10 -and ($sb.ToString() -notmatch 'D>')) { _drain 1 }

  # ---- native link (multi-archive); wait for the D> prompt to return ----
  Write-Host "link  : $linkCmd" -ForegroundColor Cyan
  $before = ([regex]::Matches($sb.ToString(), 'D>')).Count
  _wr ("{0}`r" -f $linkCmd)
  $t0 = Get-Date
  while ($true) {
    _drain 2
    if (([regex]::Matches($sb.ToString(), 'D>')).Count -gt $before) { break }
    if (((Get-Date) - $t0).TotalSeconds -ge $MaxLinkWait) { break }
  }
  $linkSecs = [int]((Get-Date) - $t0).TotalSeconds

  # ---- run the linked program; wait for D> again ----
  Write-Host "run   : $Run" -ForegroundColor Cyan
  $before = ([regex]::Matches($sb.ToString(), 'D>')).Count
  _wr ("{0}`r" -f $Run)
  $t0 = Get-Date
  while ($true) {
    _drain 2
    if (([regex]::Matches($sb.ToString(), 'D>')).Count -gt $before) { break }
    if (((Get-Date) - $t0).TotalSeconds -ge $MaxRunWait) { break }
  }
  _drain 2
  Set-Content -Path $log -Value $sb.ToString() -Encoding ASCII
} finally {
  if ($ns) { $ns.Close() }
  if ($client) { $client.Close() }
  try { $p.Kill() } catch {}
  try { $p.WaitForExit() } catch {}
  Stop-AllSim
}

$logText = ($sb.ToString()) -replace "`r",""
Write-Host "===== CP/M-68K console ====="
Write-Host $logText.Trim()
Write-Host "============================"

# ---- extract the linked .68K and report its DRI header ----
$got = Join-Path $work $out68
Remove-Item $got -ErrorAction SilentlyContinue
& $cpmcp -f $fmt $dataImg "0:$out68" $got 2>&1 | Out-Null
if (Test-Path $got) {
  $b = [IO.File]::ReadAllBytes($got)
  $magic = ([int]$b[0] -shl 8) -bor [int]$b[1]   # cast: [byte] -shl truncates to 8 bits
  Write-Host ("LINK output {0}: {1} bytes, DRI magic=0x{2:X4} (expect 0x601A)" -f $out68, $b.Length, $magic)
} else {
  Write-Host "LINK produced no $out68" -ForegroundColor Red
}

# ---- verdict: linker errors + expected program output ----
$linkBad = $logText -match 'undefined symbol|out of (memory|space)|no space|LKE_|LINK:.*error|PANIC|Bus Error|Address (error|exception)'
$rc = 0
if ($linkBad) { Write-Host "LINK: reported an error (see console) -- FAIL" -ForegroundColor Red; $rc = 1 }
foreach ($e in $Expect) {
  $pat = [Management.Automation.WildcardPattern]::Escape($e)
  if ($logText -like "*$pat*") { Write-Host "RUN: found '$e'" -ForegroundColor Green }
  else { Write-Host "RUN: MISSING '$e'" -ForegroundColor Red; $rc = 1 }
}
Write-Host ("link took ~{0}s" -f $linkSecs)
if ($rc -eq 0) { Write-Host "run-link-68k: PASS (multi-archive native link + run)" -ForegroundColor Green }
else { Write-Host "run-link-68k: FAIL" -ForegroundColor Red }
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
exit $rc
