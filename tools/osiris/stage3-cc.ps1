#requires -version 5
<#
.SYNOPSIS
  P10 stage3: run the self-hosted CC.PRG under sim68k to recompile the compiler's
  OWN source, and prove the resulting objects are byte-identical to stage2.

.DESCRIPTION
  For each compiler translation unit (and libc.c) this harness:

    1. preprocesses the source on the host to a SELF-CONTAINED file (c68k -E),
       so the on-target compile needs no headers, -I paths or FAT12 subdirs;
    2. cross-compiles that preprocessed file with the host c68k to a REFERENCE
       object (identical to the stage2 object -- verified: -E output round-trips);
    3. stages CC.PRG on the boot floppy (fd0=A:) and the preprocessed source on a
       BLANK 1.44 MB data floppy (fd1=B:) -- B: has room for CC's big scratch .s;
    4. boots Osiris, switches to B:, runs `A:CC -c X.C -o X.O`, then reads B: back
       and byte-compares X.O to the reference.

  A byte-identical match for every TU is the per-file stage2 == stage3 result.

  parse.c is the stress case: its intermediate assembly is ~646 KB and its object
  ~183 KB, which is why the source + scratch live on the empty B: disk, not the
  nearly-full boot floppy.
#>
[CmdletBinding()]
param(
  [string[]]$Tu = @('strings','hashmap','unicode','type','main','tokenize','preprocess','codegen68k','emit_elf','parse','errno','signal','time'),
  [string]$CcPrg = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-cc\CC.PRG'),
  [string]$Cc = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$Cpu = '68000',
  [string]$Mem = 'MAX',
  [int]$BootWait = 5,
  [int]$DirWait = 5,
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
  'errno'      = @{ f8 = 'ERRNO.C';    src = 'libc\core\errno.c';  self = $false }
  'signal'     = @{ f8 = 'SIGNAL.C';   src = 'libc\core\signal.c'; self = $false }
  'time'       = @{ f8 = 'TIME.C';     src = 'libc\core\time.c';   self = $false }
}

# ---- FAT12 helpers (verbatim from smoke-cc.ps1) ----
function _w16($a,$o,$v){ $a[$o]=[byte]($v -band 0xFF); $a[$o+1]=[byte](($v -shr 8) -band 0xFF) }
function _w32($a,$o,$v){ _w16 $a $o ($v -band 0xFFFF); _w16 $a ($o+2) (($v -shr 16) -band 0xFFFF) }
function _fat12get($a,$base,$cl){
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) { (($a[$off] -shr 4) -band 0x0F) -bor (($a[$off+1] -band 0xFF) -shl 4) }
  else             { ($a[$off] -band 0xFF) -bor (($a[$off+1] -band 0x0F) -shl 8) }
}
function _fat12set($a,$base,$cl,$v){
  $off = $base + $cl + ($cl -shr 1)
  if ($cl -band 1) {
    $a[$off]   = [byte](($a[$off] -band 0x0F) -bor (($v -shl 4) -band 0xF0))
    $a[$off+1] = [byte](($v -shr 4) -band 0xFF)
  } else {
    $a[$off]   = [byte]($v -band 0xFF)
    $a[$off+1] = [byte](($a[$off+1] -band 0xF0) -bor (($v -shr 8) -band 0x0F))
  }
}
function Name11([string]$name){
  $dot = $name.LastIndexOf('.')
  if ($dot -ge 0) { $b = $name.Substring(0,$dot); $e = $name.Substring($dot+1) } else { $b = $name; $e = '' }
  ($b.PadRight(8).Substring(0,8) + $e.PadRight(3).Substring(0,3)).ToUpper()
}
function Add-Fat12File($img, [string]$name11, [byte[]]$data){
  $FatSz=9; $RootEnts=224; $Bpc=512
  $f1 = 512; $f2 = (1+$FatSz)*512
  $rootLba = 1 + 2*$FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts*32/512.0)
  $dataLba = $rootLba + $rootSecs
  $need = [int][math]::Ceiling($data.Length / [double]$Bpc); if ($need -lt 1) { $need = 1 }
  # collect free clusters (2..)
  $maxCl = ((2880 - $dataLba) )  # clusters available
  $free = @()
  for ($c=2; $c -lt $maxCl+2 -and $free.Count -lt $need; $c++){ if ((_fat12get $img $f1 $c) -eq 0) { $free += $c } }
  if ($free.Count -lt $need) { throw "Add-Fat12File: not enough space for $name11 ($($data.Length) bytes)" }
  for ($i=0; $i -lt $need; $i++){
    $cl = $free[$i]; $nx = ($i -eq $need-1) ? 0xFFF : $free[$i+1]
    _fat12set $img $f1 $cl $nx; _fat12set $img $f2 $cl $nx
    $src = ($dataLba + ($cl-2))*$Bpc
    $n = [math]::Min($Bpc, $data.Length - $i*$Bpc)
    [Array]::Copy($data, $i*$Bpc, $img, $src, $n)
  }
  # find a free root slot
  $slot = -1
  for ($i=0; $i -lt $RootEnts; $i++){ $r = $rootLba*512 + $i*32; if ($img[$r] -eq 0 -or $img[$r] -eq 0xE5){ $slot=$i; break } }
  if ($slot -lt 0) { throw "Add-Fat12File: root directory full" }
  $r = $rootLba*512 + $slot*32
  [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img, $r)
  $img[$r+0x0B] = 0x20
  _w16 $img ($r+0x1A) $free[0]; _w32 $img ($r+0x1C) $data.Length
}
function Read-Fat12File($img, [string]$name11){
  $FatSz=9; $RootEnts=224; $Bpc=512
  $f1 = 512
  $rootLba = 1 + 2*$FatSz
  $rootSecs = [int][math]::Ceiling($RootEnts*32/512.0)
  $dataLba = $rootLba + $rootSecs
  for ($i=0; $i -lt $RootEnts; $i++){
    $r = $rootLba*512 + $i*32
    $b = $img[$r]
    if ($b -eq 0x00 -or $b -eq 0xE5) { continue }
    if ([Text.Encoding]::ASCII.GetString($img, $r, 11) -ne $name11) { continue }
    $size = ($img[$r+0x1C] -band 0xFF) -bor (($img[$r+0x1D] -band 0xFF) -shl 8) `
          -bor (($img[$r+0x1E] -band 0xFF) -shl 16) -bor (($img[$r+0x1F] -band 0xFF) -shl 24)
    $cl = ($img[$r+0x1A] -band 0xFF) -bor (($img[$r+0x1B] -band 0xFF) -shl 8)
    $out = New-Object byte[] $size; $got = 0
    while ($cl -ge 2 -and $cl -lt 0xFF0 -and $got -lt $size){
      $src = ($dataLba + ($cl-2))*512
      $n = [math]::Min($Bpc, $size - $got)
      [Array]::Copy($img, $src, $out, $got, $n)
      $got += $n
      $cl = _fat12get $img $f1 $cl
    }
    return ,$out
  }
  return $null
}
function New-BlankFat12([string]$path){
  $img = New-Object byte[] 1474560
  $bs  = [IO.File]::ReadAllBytes((Join-Path $simenv 'osiris-boot-144.img'))
  [Array]::Copy($bs, 0, $img, 0, 512)                 # BPB + (unused) boot code
  foreach ($f in @(512, (512+9*512))) { $img[$f]=0xF0; $img[$f+1]=0xFF; $img[$f+2]=0xFF }
  [IO.File]::WriteAllBytes($path, $img)
  return $img
}
function Stop-AllSim {
  $deadline=(Get-Date).AddMilliseconds(6000)
  while((Get-Date) -lt $deadline){ $ps=Get-Process c68k-sim68k -ErrorAction SilentlyContinue; if(-not $ps){break}; $ps|Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 200 }
  Start-Sleep -Milliseconds 600
}

# ---- ensure simenv + CC.PRG ----
if (-not (Test-Path (Join-Path $simenv 'c68k-sim68k.exe'))) { & (Join-Path $repo 'tools\bootstrap-simenv.ps1') }
$sim=Join-Path $simenv 'c68k-sim68k.exe'; $rom=Join-Path $simenv 'bootrom.bin'; $baseImg=Join-Path $simenv 'osiris-boot-144.img'
if (-not (Test-Path $CcPrg)) { & (Join-Path $PSScriptRoot 'build-cc.ps1'); if ($LASTEXITCODE -ne 0) { throw 'build-cc.ps1 failed' } }
foreach($pth in @($sim,$rom,$baseImg,$CcPrg,$Cc)){ if(-not (Test-Path $pth)){ throw "stage3: missing '$pth'" } }

$work=Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-stage3'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$inc  = Join-Path $repo 'include'; $linc = Join-Path $repo 'libc\include'; $src = Join-Path $repo 'src'

function Invoke-Stage3One($name){
  $e = $TUMAP[$name]; if (-not $e) { throw "unknown TU '$name'" }
  $f8 = $e.f8; $srcPath = Join-Path $repo $e.src
  $ppFile = Join-Path $work $f8
  $objName = [IO.Path]::ChangeExtension($f8, '.O')
  $refObj = Join-Path $work ("REF_" + [IO.Path]::ChangeExtension($f8,'.O'))

  # 1. preprocess to a self-contained file
  $ppArgs = @('-E')
  if ($e.self) { $ppArgs += @('-DC68K_SELFHOST', "-I$inc", "-I$linc", "-I$src") } else { $ppArgs += @("-I$linc") }
  $ppArgs += @($srcPath)
  $ppText = & $Cc @ppArgs 2>$null
  if ($LASTEXITCODE -ne 0) { throw "preprocess $name failed" }
  Set-Content -Path $ppFile -Value $ppText -Encoding ASCII
  $ppLen = (Get-Item $ppFile).Length

  # 2. host reference from the SAME preprocessed file
  & $Cc -fintegrated-as -c $ppFile -o $refObj 2>$null
  if ($LASTEXITCODE -ne 0) { throw "reference compile $name failed" }
  $refBytes = [IO.File]::ReadAllBytes($refObj)

  # 3. stage fd0 = boot + CC.PRG ; fd1 = blank + preprocessed source
  Stop-AllSim
  $imgA = Join-Path $work 'a.img'; $imgB = Join-Path $work 'b.img'
  $log = Join-Path $work 'con.log'; $rtc = Join-Path $work 'rtc.nv'
  $bz = [IO.File]::ReadAllBytes($baseImg)
  Add-Fat12File $bz (Name11 'CC.PRG') ([IO.File]::ReadAllBytes($CcPrg))
  [IO.File]::WriteAllBytes($imgA, $bz)
  $bb = New-BlankFat12 $imgB
  Add-Fat12File $bb (Name11 $f8) ([IO.File]::ReadAllBytes($ppFile))
  [IO.File]::WriteAllBytes($imgB, $bb)
  Remove-Item $log -ErrorAction SilentlyContinue
  if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc,(New-Object byte[] 64)) }

  # 4. boot, switch to B:, compile on-target -- driven over the sim's TCP ACIA
  #    console. It is the only LIVE, lock-free, unbuffered channel: the --tee-acia
  #    file is held with an exclusive lock (verified), and stdout is block-buffered
  #    when piped, so neither can be polled for the shell prompt. A socket can.
  $port = 8000 + (Get-Random -Minimum 0 -Maximum 1500)
  $psi=New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=$sim
  foreach($a in @('--cpu',$Cpu,'--mem',$Mem,"--rom:$rom",'--fd0',$imgA,'--fd1',$imgB,'--acia-port','none','--acia-tcp-port',"$port",'--fdc-threads','off','--rtc-nv',$rtc)){ [void]$psi.ArgumentList.Add($a) }
  $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true; $psi.UseShellExecute=$false
  $p=New-Object System.Diagnostics.Process; $p.StartInfo=$psi
  [void]$p.Start()
  $sb=New-Object System.Text.StringBuilder
  $client=$null; $ns=$null; $rbuf=New-Object byte[] 8192
  # Poll returns as soon as the B: shell prompt reappears (compile done); the cap
  # is only a hang/OOM guard. On-target compile ~6 s per KB of output object.
  $maxWait = [int]([math]::Max(300, ($refBytes.Length/1024.0) * 14))
  $ccCmd = "A:CC -c $f8 -o $objName"
  $cwait = 0; $detected=$false
  try {
    for ($i=0; $i -lt 60; $i++) { try { $client=New-Object System.Net.Sockets.TcpClient; $client.Connect('127.0.0.1',$port); break } catch { Start-Sleep -Milliseconds 250; $client=$null } }
    if ($null -eq $client) { throw "stage3: could not connect to sim ACIA on tcp/$port" }
    $ns=$client.GetStream()
    function _drain($secs){ $t=[Diagnostics.Stopwatch]::StartNew(); while($t.Elapsed.TotalSeconds -lt $secs){ if($ns.DataAvailable){ $n=$ns.Read($rbuf,0,$rbuf.Length); if($n -gt 0){ [void]$sb.Append([Text.Encoding]::ASCII.GetString($rbuf,0,$n)) } } else { Start-Sleep -Milliseconds 80 } } }
    function _wr($s){ $b=[Text.Encoding]::ASCII.GetBytes($s); $ns.Write($b,0,$b.Length); $ns.Flush() }
    # boot to the A> prompt (give the guest up to $BootWait+35 s)
    $t=[Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt ($BootWait+35) -and ($sb.ToString() -notmatch 'A>')) { _drain 1 }
    _wr "B:`r"
    $t=[Diagnostics.Stopwatch]::StartNew(); while ($t.Elapsed.TotalSeconds -lt 10 -and ($sb.ToString() -notmatch 'B>')) { _drain 1 }
    $before = ([regex]::Matches($sb.ToString(), 'B>')).Count
    _wr ("{0}`r" -f $ccCmd)
    $t0 = Get-Date
    while ($true) {
      _drain 2
      $now = ([regex]::Matches($sb.ToString(), 'B>')).Count
      $cwait = [int]((Get-Date) - $t0).TotalSeconds
      if ($now -gt $before) { $detected=$true; break }   # CC returned to the prompt
      if ($cwait -ge $maxWait) { break }                 # timeout guard
    }
    _wr "DIR`r"
    _drain $DirWait
    Set-Content -Path $log -Value $sb.ToString() -Encoding ASCII   # persist console for debugging
  } finally {
    if ($ns) { $ns.Close() }
    if ($client) { $client.Close() }
    try { $p.Kill() } catch {}
    $p.WaitForExit(); Stop-AllSim
  }

  # 5. read B: back, compare
  $outBz=[IO.File]::ReadAllBytes($imgB)
  $objBytes = Read-Fat12File $outBz (Name11 $objName)
  $res = [pscustomobject]@{ TU=$name; File=$f8; PP=$ppLen; Ref=$refBytes.Length; Got=$(if($objBytes){$objBytes.Length}else{0}); Status='' ; Wait=$cwait }
  if ($null -eq $objBytes) { $res.Status = 'FAIL(no .o)'; return $res }
  if ($KeepArtifacts) { [IO.File]::WriteAllBytes((Join-Path $work $objName), $objBytes) }
  if ($objBytes.Length -ne $refBytes.Length) { $res.Status = "FAIL(size $($objBytes.Length) vs $($refBytes.Length))"; return $res }
  $diff = -1
  for ($i=0; $i -lt $refBytes.Length; $i++){ if ($objBytes[$i] -ne $refBytes[$i]){ $diff=$i; break } }
  $res.Status = if ($diff -ge 0) { "FAIL(byte@$diff)" } else { 'PASS' }
  return $res
}

$results = @()
foreach ($name in $Tu) {
  Write-Host ("stage3: {0} ..." -f $name) -ForegroundColor Cyan
  $r = Invoke-Stage3One $name
  Write-Host ("  {0,-11} pp={1,7} ref={2,6} got={3,6} wait={4,3}s  {5}" -f $r.TU,$r.PP,$r.Ref,$r.Got,$r.Wait,$r.Status) -ForegroundColor $(if($r.Status -eq 'PASS'){'Green'}else{'Red'})
  $results += $r
}
Write-Host "==================== stage3 summary ===================="
$results | ForEach-Object { "{0,-11} {1,-10} ref={2,6} got={3,6}  {4}" -f $_.TU,$_.File,$_.Ref,$_.Got,$_.Status }
$pass = ($results | Where-Object { $_.Status -eq 'PASS' }).Count
Write-Host ("stage3: {0}/{1} byte-identical (stage2 == stage3)" -f $pass, $results.Count) -ForegroundColor $(if($pass -eq $results.Count){'Green'}else{'Yellow'})
if (-not $KeepArtifacts) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
