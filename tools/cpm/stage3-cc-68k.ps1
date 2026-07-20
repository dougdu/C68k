#requires -version 5
<#
.SYNOPSIS
  P10 CP/M stage3: run the self-hosted CC.68K under sim68k to recompile the
  compiler's OWN source on CP/M-68K, and prove the objects are byte-identical
  to the cross-compiler's (per-file stage2 == stage3 on CP/M).

.DESCRIPTION
  Mirrors tools/osiris/stage3-cc.ps1 but for CP/M-68K. For each translation
  unit (and libc.c) this harness:

    1. preprocesses the source on the host to a SELF-CONTAINED file (c68k -E),
       so the on-target compile needs no headers, -I paths, or (CP/M has none)
       directories;
    2. cross-compiles that preprocessed file with the host c68k to a REFERENCE
       object -- the compiler's .o is OS-neutral, so this is identical to both
       the Osiris and the CP/M on-target object;
    3. deploys CC.68K + the preprocessed source onto a scratch CP/M hard disk
       (drive D: = scsi1) with cpmcp;
    4. boots CP/M-68K, selects D:, runs `CC -c X.C -o X.O`, waits (over the
       sim's TCP ACIA console) for the D> prompt to return, then extracts X.O
       from the disk and byte-compares it to the reference.

  D: (an ~8 MB CP/M volume) has ample room for CC's large scratch assembly
  (parse.c's intermediate .s is ~646 KB), unlike the space-tight Osiris floppy.
#>
[CmdletBinding()]
param(
  [string[]]$Tu = @('strings','hashmap','unicode','type','main','tokenize','preprocess','codegen68k','emit_elf','parse','libc'),
  [string]$Cc68k = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc68k\CC.68K'),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [int]$BootWait = 9,
  [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'

# `pwsh -File` passes `-Tu a,b,c` as a single string; normalise to a real list.
$Tu = @($Tu | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })

# ---- TU table: 8.3 name, source path, and whether -DC68K_SELFHOST applies ----
$TUMAP = [ordered]@{
  'main'       = @{ f8 = 'MAIN.C';     src = 'src\main.c';       self = $true  }
  'strings'    = @{ f8 = 'STRINGS.C';  src = 'src\strings.c';    self = $true  }
  'hashmap'    = @{ f8 = 'HASHMAP.C';  src = 'src\hashmap.c';    self = $true  }
  'unicode'    = @{ f8 = 'UNICODE.C';  src = 'src\unicode.c';    self = $true  }
  'type'       = @{ f8 = 'TYPE.C';     src = 'src\type.c';       self = $true  }
  'tokenize'   = @{ f8 = 'TOKENIZE.C'; src = 'src\tokenize.c';   self = $true  }
  'preprocess' = @{ f8 = 'PREPROC.C';  src = 'src\preprocess.c'; self = $true  }
  'parse'      = @{ f8 = 'PARSE.C';    src = 'src\parse.c';      self = $true  }
  'codegen68k' = @{ f8 = 'CODEGEN.C';  src = 'src\codegen68k.c'; self = $true  }
  'emit_elf'   = @{ f8 = 'EMIT_ELF.C'; src = 'src\emit_elf.c';   self = $true  }
  'libc'       = @{ f8 = 'LIBC.C';     src = 'libc\core\libc.c'; self = $false }
}

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
$flop  = Join-Path $simenv 'cpmboot144.img'
$scsi0 = Join-Path $simenv 'scsi0.img'
$scsi1 = Join-Path $simenv 'scsi1.img'
$cpmrm = Join-Path $simenv 'cpmrm.exe'
$cpmcp = Join-Path $simenv 'cpmcp.exe'
$fmt   = 'worm68k-8m'
foreach ($p in @($sim,$rom,$flop,$scsi0,$scsi1,$cpmrm,$cpmcp,$Cc68k,$Cc)) {
  if (-not (Test-Path $p)) { throw "stage3-68k: missing asset '$p'" }
}
$inc  = Join-Path $repo 'libc\include'
$binc = Join-Path $repo 'include'
$srcd = Join-Path $repo 'src'
$work = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-stage3-cpm'
New-Item -ItemType Directory -Force -Path $work | Out-Null

function Invoke-Stage3One($name) {
  if (-not $TUMAP.Contains($name)) { throw "unknown TU '$name'" }
  $f8 = $TUMAP[$name].f8
  $srcPath = Join-Path $repo $TUMAP[$name].src
  $objName = [IO.Path]::GetFileNameWithoutExtension($f8) + '.O'
  $ppFile  = Join-Path $work $f8
  $refObj  = Join-Path $work ("REF_" + $objName)

  # 1. preprocess on the host to a self-contained file
  $ppArgs = @('-E')
  if ($TUMAP[$name].self) { $ppArgs += '-DC68K_SELFHOST'; $ppArgs += @("-I$binc","-I$inc","-I$srcd") }
  else { $ppArgs += @("-I$inc") }
  $ppText = & $Cc @ppArgs $srcPath
  if ($LASTEXITCODE -ne 0) { throw "preprocess $name failed" }
  # NB: no -NoNewline. Set-Content -NoNewline concatenates the captured line
  # array with no separators, merging adjacent-line tokens (e.g. two identifiers
  # across a line break) -- which silently miscompiles (the host reference of
  # type.c failed "undefined variable"). Writing one element per line matches
  # the Osiris harness; the tokenizer canonicalises CRLF on both host and target.
  Set-Content -Path $ppFile -Value $ppText -Encoding ASCII
  $ppLen = (Get-Item $ppFile).Length

  # 2. host reference object (OS-neutral -- same bytes the target must emit)
  & $Cc -fintegrated-as -c $ppFile -o $refObj | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "host reference compile $name failed" }
  $refBytes = [IO.File]::ReadAllBytes($refObj)

  # 3. deploy CC.68K + the preprocessed source onto a scratch scsi1 (drive D:)
  Stop-AllSim
  $bootImg = Join-Path $work 'cpmboot.img'
  $dataImg = Join-Path $work 'scsi1.img'
  $log     = Join-Path $work 'con.log'
  Copy-Item $flop  $bootImg -Force
  Copy-Item $scsi1 $dataImg -Force
  Remove-Item $log -ErrorAction SilentlyContinue
  & $cpmrm -f $fmt $dataImg '0:CC.68K'   2>&1 | Out-Null
  & $cpmrm -f $fmt $dataImg "0:$f8"       2>&1 | Out-Null
  & $cpmrm -f $fmt $dataImg "0:$objName"  2>&1 | Out-Null
  & $cpmcp -f $fmt $dataImg $Cc68k '0:CC.68K' 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "cpmcp CC.68K failed" }
  & $cpmcp -f $fmt $dataImg $ppFile "0:$f8" 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "cpmcp $f8 failed" }

  # 4. boot CP/M, select D:, compile, poll the D> prompt over the TCP ACIA console
  $port = 8000 + (Get-Random -Minimum 0 -Maximum 1500)
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $sim
  foreach ($a in @('--cpu','68008','--mem','MAX',"--rom:$rom",'--acia-port','none','--acia-cts','tied','--acia-tcp-port',"$port",'--fdc-threads','on','--fd0',$bootImg,'--scsi0',$scsi0,'--scsi1',$dataImg)) { [void]$psi.ArgumentList.Add($a) }
  $psi.WorkingDirectory = $simenv
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
  $p = New-Object System.Diagnostics.Process; $p.StartInfo = $psi
  [void]$p.Start()
  $sb = New-Object System.Text.StringBuilder
  $client = $null; $ns = $null; $rbuf = New-Object byte[] 8192
  # compile ~3 s per KB of output object on the 68008; cap is only a hang guard
  $maxWait = [int]([math]::Max(300, ($refBytes.Length / 1024.0) * 16))
  $ccCmd = "CC -c $f8 -o $objName"
  $cwait = 0
  try {
    for ($i = 0; $i -lt 60; $i++) { try { $client = New-Object System.Net.Sockets.TcpClient; $client.Connect('127.0.0.1', $port); break } catch { Start-Sleep -Milliseconds 250; $client = $null } }
    if ($null -eq $client) { throw "could not connect to sim ACIA on tcp/$port" }
    $ns = $client.GetStream()
    function _drain($secs) { $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt $secs) { if ($ns.DataAvailable) { $n = $ns.Read($rbuf, 0, $rbuf.Length); if ($n -gt 0) { [void]$sb.Append([Text.Encoding]::ASCII.GetString($rbuf, 0, $n)) } } else { Start-Sleep -Milliseconds 80 } } }
    function _wr($s) { $b = [Text.Encoding]::ASCII.GetBytes($s); $ns.Write($b, 0, $b.Length); $ns.Flush() }
    # boot to the A> prompt
    $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt ($BootWait + 35) -and ($sb.ToString() -notmatch 'A>')) { _drain 1 }
    _wr "D:`r"
    $t = [Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt 10 -and ($sb.ToString() -notmatch 'D>')) { _drain 1 }
    $before = ([regex]::Matches($sb.ToString(), 'D>')).Count
    _wr ("{0}`r" -f $ccCmd)
    $t0 = Get-Date
    while ($true) {
      _drain 2
      $now = ([regex]::Matches($sb.ToString(), 'D>')).Count
      $cwait = [int]((Get-Date) - $t0).TotalSeconds
      if ($now -gt $before) { break }
      if ($cwait -ge $maxWait) { break }
    }
    _wr "DIR`r"
    _drain 4
    Set-Content -Path $log -Value $sb.ToString() -Encoding ASCII
  } finally {
    if ($ns) { $ns.Close() }
    if ($client) { $client.Close() }
    try { $p.Kill() } catch {}
    $p.WaitForExit(); Stop-AllSim
  }

  # 5. extract the object from D: and compare
  $gotObj = Join-Path $work $objName
  Remove-Item $gotObj -ErrorAction SilentlyContinue
  & $cpmcp -f $fmt $dataImg "0:$objName" $gotObj 2>&1 | Out-Null
  $objBytes = if (Test-Path $gotObj) { [IO.File]::ReadAllBytes($gotObj) } else { $null }
  $res = [pscustomobject]@{ TU = $name; File = $f8; PP = $ppLen; Ref = $refBytes.Length; Got = $(if ($objBytes) { $objBytes.Length } else { 0 }); Status = ''; Wait = $cwait }
  if (-not $KeepArtifacts) { Remove-Item $refObj, $ppFile -ErrorAction SilentlyContinue }
  if ($null -eq $objBytes) { $res.Status = 'FAIL(no .o)'; return $res }
  # CP/M sequential files are stored in whole 128-byte records: an object whose
  # length is not a multiple of 128 is physically padded up to the next record
  # boundary on close (base CP/M has no exact byte-count). True on-disk byte
  # identity is therefore impossible for such objects; the stage3 proof is that
  # the object CONTENT is byte-identical and the only excess is record padding.
  $refLen = $refBytes.Length
  $gotLen = $objBytes.Length
  $expectPad = (128 - ($refLen % 128)) % 128
  if ($gotLen -ne $refLen -and $gotLen -ne ($refLen + $expectPad)) {
    $res.Status = "FAIL(size $gotLen vs $refLen)"; return $res
  }
  $diff = -1
  for ($i = 0; $i -lt $refLen; $i++) { if ($objBytes[$i] -ne $refBytes[$i]) { $diff = $i; break } }
  if ($diff -ge 0) { $res.Status = "FAIL(byte@$diff)"; return $res }
  $tailOk = $true
  for ($i = $refLen; $i -lt $gotLen; $i++) { if ($objBytes[$i] -ne 0x1A -and $objBytes[$i] -ne 0x00) { $tailOk = $false; break } }
  $res.Status = if (-not $tailOk) { "FAIL(pad@tail)" } elseif ($gotLen -eq $refLen) { 'PASS' } else { 'PASS(pad)' }
  return $res
}

$results = @()
foreach ($name in $Tu) {
  Write-Host ("cpm-stage3: {0} ..." -f $name) -ForegroundColor Cyan
  $r = Invoke-Stage3One $name
  Write-Host ("  {0,-11} pp={1,7} ref={2,6} got={3,6} wait={4,4}s  {5}" -f $r.TU,$r.PP,$r.Ref,$r.Got,$r.Wait,$r.Status) -ForegroundColor $(if ($r.Status -like 'PASS*') { 'Green' } else { 'Red' })
  $results += $r
}
Write-Host "================ CP/M stage3 summary ================"
$results | ForEach-Object { "{0,-11} {1,-10} ref={2,6} got={3,6}  {4}" -f $_.TU,$_.File,$_.Ref,$_.Got,$_.Status }
$pass = ($results | Where-Object { $_.Status -like 'PASS*' }).Count
Write-Host ("CP/M stage3: {0}/{1} byte-identical (stage2 == stage3)" -f $pass, $results.Count) -ForegroundColor $(if ($pass -eq $results.Count) { 'Green' } else { 'Yellow' })
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
