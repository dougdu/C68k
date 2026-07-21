#requires -version 5
<#
.SYNOPSIS
  Reproduce the native-LINK.PRG address-error on the float archive under
  sid68k and capture the exception frame + faulting instruction (P9 finding).

.DESCRIPTION
  Boots Osiris under `sim68k --gdb` with an AUTOEXEC.BAT that runs the failing
  `LINK ... FLOAT.A`, attaches sid68k, arms the 68000 address-error catch
  (`!ex catch addr`), continues, and on the caught fault dumps the exception
  frame, registers, and a disassembly window at the fault PC.

  To symbolicate, it first re-links an UNSTRIPPED LINKDBG.PRG from the Osiris
  build objects (same *loaded* bytes as the shipped stripped LINK.PRG, plus a
  symbol table). Map a runtime PC to a source symbol by matching the fault-site
  bytes to `m68k-elf-objdump -d LINKDBG.PRG` (base = runtimePC - fileOffset).

  Root cause found with this: `lk_sym_add_object` (lk_sym.a68) walks an archive
  member's ELF .symtab in place with `move.l (a5),d1`; member dpmath.o has
  .symtab at an ODD file offset, so the in-place pointer is odd -> address error.

.NOTES
  sid68k can't E-load a static-PIE .PRG, so it ATTACHES to sim68k's RSP socket.
  sid68k block-buffers stdout, so we let it exit cleanly (EOF on stdin) and
  ReadToEnd rather than killing it (which loses the buffer).
#>
param(
  [int]$Port = 1234,
  [string]$OsirisBuild = 'C:\git\osiris\build',
  [string]$Ld       = 'C:\git\osiris\toolchain\binutils\m68k-elf-ld.exe',
  [string]$PrgLd    = 'C:\git\osiris\ld\osiris-prg.ld',
  [string]$Sim      = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\sim68k.exe',
  [string]$Sid      = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\sid68k.exe',
  [string]$Asm      = 'C:\git\worm68k\68kTools\builds\win64\bin\Release\asm68K.exe',
  [string]$Objdump  = 'C:\SysGCC\m68k-elf\bin\m68k-elf-objdump.exe',
  [string]$Cc       = (Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-p2\c68k.exe'),
  [string]$FloatLib = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lib\libm\libm.a'),
  [string]$LinkCmd  = 'LINK -o HELLO.PRG SYS.O HELLO.O RT68K.O LIBC.O FLOAT.A'
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$simenv = Join-Path $repo 'simenv'
$rom    = Join-Path $simenv 'bootrom.bin'
$baseImg= Join-Path $simenv 'osiris-boot-144.img'
$inc    = Join-Path $repo 'libc\include'
$work   = Join-Path ([System.IO.Path]::GetTempPath()) 'c68k-dbg-link'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$linkdbg = Join-Path $work 'LINKDBG.PRG'

foreach ($p in @($Sim,$Sid,$Asm,$rom,$baseImg,$FloatLib,$PrgLd,$Ld)) {
  if (-not (Test-Path $p)) { throw "missing: $p" }
}

# ---- re-link UNSTRIPPED LINKDBG.PRG (drop -s) from the Osiris build objects ----
$lkObjs = @('lk_main.o','lk_sym.o','lk_layout.o','lk_reloc.o','lk_dyn.o','lk_out.o','ol_ar.o','ol_elf.o') |
          ForEach-Object { Join-Path $OsirisBuild $_ }
$cmdlib = Join-Path $OsirisBuild 'cmdlib.a'
foreach ($o in ($lkObjs + $cmdlib)) { if (-not (Test-Path $o)) { throw "missing LINK object: $o (build LINK in the Osiris repo first)" } }
Write-Host "re-linking unstripped LINKDBG.PRG ..." -ForegroundColor Cyan
& $Ld -pie --no-dynamic-linker -z max-page-size=0x20 -T $PrgLd -o $linkdbg @lkObjs $cmdlib
if ($LASTEXITCODE -ne 0) { throw "unstripped LINK re-link failed" }

# ---- FAT12 add-one-file (proven helpers) ----
function _w16($a,$o,$v){ $a[$o]=[byte]($v -band 0xFF); $a[$o+1]=[byte](($v -shr 8) -band 0xFF) }
function _w32($a,$o,$v){ _w16 $a $o ($v -band 0xFFFF); _w16 $a ($o+2) (($v -shr 16) -band 0xFFFF) }
function _fat12get($a,$base,$cl){ $off=$base+$cl+($cl -shr 1); if($cl -band 1){ (($a[$off] -shr 4) -band 0x0F) -bor (($a[$off+1] -band 0xFF) -shl 4) } else { ($a[$off] -band 0xFF) -bor (($a[$off+1] -band 0x0F) -shl 8) } }
function _fat12set($a,$base,$cl,$v){ $off=$base+$cl+($cl -shr 1); if($cl -band 1){ $a[$off]=[byte](($a[$off] -band 0x0F) -bor (($v -shl 4) -band 0xF0)); $a[$off+1]=[byte](($v -shr 4) -band 0xFF) } else { $a[$off]=[byte]($v -band 0xFF); $a[$off+1]=[byte](($a[$off+1] -band 0xF0) -bor (($v -shr 8) -band 0x0F)) } }
function Name11([string]$n){ $d=$n.LastIndexOf('.'); $s=$n;$e=''; if($d -ge 0){$s=$n.Substring(0,$d);$e=$n.Substring($d+1)}; if($s.Length -gt 8){$s=$s.Substring(0,8)}; ($s.ToUpper().PadRight(8))+($e.ToUpper().PadRight(3)) }
function Add-Fat12File($img,[string]$name11,[byte[]]$data){
  $FatSz=9;$RootEnts=224;$Bpc=512;$f1=512;$f2=(1+$FatSz)*512;$rootLba=1+2*$FatSz
  $rootSecs=[int][math]::Ceiling($RootEnts*32/512.0);$dataLba=$rootLba+$rootSecs;$maxCl=(2880-$dataLba)+1
  $ncl=[int][math]::Ceiling($data.Length/[double]$Bpc); if($ncl -lt 1){$ncl=1}
  $chain=@(); for($c=2;$c -le $maxCl -and $chain.Count -lt $ncl;$c++){ if((_fat12get $img $f1 $c) -eq 0){$chain+=$c} }
  if($chain.Count -lt $ncl){ throw "no clusters" }
  $slot=-1; for($i=0;$i -lt $RootEnts;$i++){ $bb=$img[$rootLba*512+$i*32]; if($bb -eq 0 -or $bb -eq 0xE5){$slot=$i;break} }
  for($i=0;$i -lt $ncl;$i++){ $c=$chain[$i]; $dst=($dataLba+($c-2))*512; $n=[math]::Min($Bpc,$data.Length-$i*$Bpc); if($n -gt 0){[Array]::Copy($data,$i*$Bpc,$img,$dst,$n)}; $nv= if($i -lt ($ncl-1)){$chain[$i+1]}else{0xFFF}; _fat12set $img $f1 $c $nv; _fat12set $img $f2 $c $nv }
  $r=$rootLba*512+$slot*32; [Text.Encoding]::ASCII.GetBytes($name11).CopyTo($img,$r); $img[$r+0x0B]=0x20; _w16 $img ($r+0x1A) $chain[0]; _w32 $img ($r+0x1C) $data.Length
}

# ---- build the objects that reproduce the fault ----
Write-Host "building objects..." -ForegroundColor Cyan
& $Asm /Cx /elf /c /nologo "/Fo$work\SYS.O"   (Join-Path $repo 'libc\osiris\osiris_sys.a68') 2>&1 | Out-Null
& $Asm /Cx /elf /c /nologo "/Fo$work\RT68K.O" (Join-Path $repo 'lib\runtime\rt68k.a68')      2>&1 | Out-Null
& $Cc -fintegrated-as -c (Join-Path $repo 'libc\core\libc.c') -o "$work\LIBC.O"  "-I$inc"    2>&1 | Out-Null
& $Cc -fintegrated-as -c (Join-Path $repo 'samples\hello.c')  -o "$work\HELLO.O" "-I$inc"    2>&1 | Out-Null

# ---- stage disk: objects + FLOAT.A + AUTOEXEC.BAT that runs the failing LINK ----
Write-Host "staging disk..." -ForegroundColor Cyan
$img = "$work\os.img"; $log = "$work\sim.log"; $rtc = "$work\rtc.nv"
$bz = [IO.File]::ReadAllBytes($baseImg)
Add-Fat12File $bz (Name11 'LINK.PRG')     ([IO.File]::ReadAllBytes($linkdbg))
Add-Fat12File $bz (Name11 'SYS.O')        ([IO.File]::ReadAllBytes("$work\SYS.O"))
Add-Fat12File $bz (Name11 'RT68K.O')      ([IO.File]::ReadAllBytes("$work\RT68K.O"))
Add-Fat12File $bz (Name11 'LIBC.O')       ([IO.File]::ReadAllBytes("$work\LIBC.O"))
Add-Fat12File $bz (Name11 'HELLO.O')      ([IO.File]::ReadAllBytes("$work\HELLO.O"))
Add-Fat12File $bz (Name11 'FLOAT.A')      ([IO.File]::ReadAllBytes($FloatLib))
Add-Fat12File $bz (Name11 'AUTOEXEC.BAT') ([Text.Encoding]::ASCII.GetBytes("$LinkCmd`r`n"))
[IO.File]::WriteAllBytes($img, $bz)
if (-not (Test-Path $rtc)) { [IO.File]::WriteAllBytes($rtc,(New-Object byte[] 64)) }
Remove-Item $log -ErrorAction SilentlyContinue
Get-Process sim68k,sid68k,c68k-sim68k -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# ---- start sim68k --gdb (waits for the debugger) ----
Write-Host "starting sim68k --gdb $Port ..." -ForegroundColor Cyan
$simPsi = New-Object System.Diagnostics.ProcessStartInfo
$simPsi.FileName = $Sim
foreach ($a in @("--rom:$rom",'--fd0',$img,'--acia-port','none','--fdc-threads','off','--rtc-nv',$rtc,'--tee-acia',$log,'--gdb',"$Port")) { [void]$simPsi.ArgumentList.Add($a) }
$simPsi.RedirectStandardInput = $true; $simPsi.RedirectStandardOutput = $true; $simPsi.RedirectStandardError = $true; $simPsi.UseShellExecute = $false
$simProc = [System.Diagnostics.Process]::Start($simPsi)
Start-Sleep -Seconds 2

# ---- attach sid68k, drive it, capture on clean exit ----
Write-Host "attaching sid68k ..." -ForegroundColor Cyan
$sidPsi = New-Object System.Diagnostics.ProcessStartInfo
$sidPsi.FileName = $Sid
[void]$sidPsi.ArgumentList.Add('--sim'); [void]$sidPsi.ArgumentList.Add("localhost:$Port")
$sidPsi.RedirectStandardInput = $true; $sidPsi.RedirectStandardOutput = $true; $sidPsi.RedirectStandardError = $true; $sidPsi.UseShellExecute = $false
$sidProc = [System.Diagnostics.Process]::Start($sidPsi)
Start-Sleep -Seconds 2

$cmds = @('!ex catch addr on','!ex verbose on','G','!ex','X','L a8202,a8236','DW a81f0,a8240')
foreach ($c in $cmds) { $sidProc.StandardInput.WriteLine($c) }
$sidProc.StandardInput.Flush(); $sidProc.StandardInput.Close()
$out = ''
if ($sidProc.WaitForExit(40000)) {
  $out = $sidProc.StandardOutput.ReadToEnd() + $sidProc.StandardError.ReadToEnd()
} else { try { $sidProc.Kill() } catch {}; Start-Sleep -Milliseconds 500; try { $out = $sidProc.StandardOutput.ReadToEnd() } catch {} }
try { $simProc.Kill() } catch {}
Get-Process sim68k,sid68k -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700

Write-Host "===================== sid68k session =====================" -ForegroundColor Green
Write-Host $out
Write-Host "===================== sim68k console tail =================" -ForegroundColor Green
if (Test-Path $log) { Get-Content $log -Tail 6 }
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "unstripped LINK for symbolication: $linkdbg  (objdump -d to map the fault PC)"
