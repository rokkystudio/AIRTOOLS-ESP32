<#
.SYNOPSIS
    Rebuilds ESP32-C6 libnet80211.a with the raw-frame whitelist patch
    (Disassociation 0xA0 + Deauthentication 0xC0).

.DESCRIPTION
    The patch rewrites three 4-byte instructions inside
    ieee80211_raw_frame_sanity_check() in ieee80211_output.o so that the
    management-subtype whitelist also accepts 0xA0 and 0xC0.

    Two archive variants are recognised automatically by the hash of the
    ieee80211_output.o member:

      * libs 5.3.0 + sha.083aad99cf (IDF release_v5.3, 52 members)
      * the older 1.48 MB archive that was unpacked into libnet80211_unpacked

    The script extracts the member, verifies the original words, patches
    them, repacks the archive and verifies that every other member is
    byte-identical. With -Install it replaces the PlatformIO copy and
    keeps backups.

.EXAMPLE
    .\apply_net80211_patch.ps1
    .\apply_net80211_patch.ps1 -Install
#>
[CmdletBinding()]
param(
    [string]$SourceArchive = 'C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a',
    [string]$OutDir        = 'D:\PROJECTS\AIRTOOLS-ESP32\analysis\net80211\patchcheck',
    [string]$BackupDir     = 'D:\PROJECTS\AIRTOOLS-ESP32\analysis\net80211\backup',
    [string]$ToolchainBin  = 'C:\Users\rokky\.platformio\packages\toolchain-riscv32-esp\bin',
    [switch]$Install,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$MemberName = 'ieee80211_output.o'

# Variant 1: framework-arduinoespressif32-libs 5.3.0+sha.083aad99cf (current build).
$Variants = @(
    @{
        Name      = 'esp32c6 libs 5.3.0 (IDF release_v5.3)'
        MemberSha = 'f750817425dd3f454c54fb245c6f689425739557a739f274e6545e0a5afb9c25'
        Patches   = @(
            @{ Offset = 0x3094; Old = 0x0F06F793; New = 0x0C06F793; Name = 'andi a5,a3,0xC0' },
            @{ Offset = 0x30D0; Old = 0x0D000693; New = 0x0C000613; Name = 'li   a2,0xC0' },
            @{ Offset = 0x30D4; Old = 0x02D78C63; New = 0x02C68C63; Name = 'beq  a3,a2,.L1040' }
        )
    },
    @{
        Name      = 'older 1.48 MB archive (50 members)'
        MemberSha = '83c0057a24523e0cc43c0058c2cec3e95cfeae81af8a53e7ccf27e02a98011c4'
        Patches   = @(
            @{ Offset = 0x32B8; Old = 0x0F067793; New = 0x0C067793; Name = 'andi a5,a2,0xC0' },
            @{ Offset = 0x32F4; Old = 0x0D000693; New = 0x0C000693; Name = 'li   a3,0xC0' },
            @{ Offset = 0x32F8; Old = 0x02D78C63; New = 0x02D60C63; Name = 'beq  a2,a3,.L1077' }
        )
    }
)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Write-Step([string]$Text) {
    Write-Host "==> $Text"
}

$ar     = Join-Path $ToolchainBin 'riscv32-esp-elf-ar.exe'
$ranlib = Join-Path $ToolchainBin 'riscv32-esp-elf-ranlib.exe'

foreach ($tool in @($ar, $ranlib)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Toolchain binary not found: $tool" }
}
if (-not (Test-Path -LiteralPath $SourceArchive)) { throw "Source archive not found: $SourceArchive" }
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$sourceSha = Get-Sha256 $SourceArchive
Write-Step "Source archive: $SourceArchive"
Write-Step "Source sha256 : $sourceSha"

# --- 1. Extract the member -------------------------------------------------
$workRoot = Join-Path $OutDir 'work'
if (Test-Path -LiteralPath $workRoot) { Remove-Item -LiteralPath $workRoot -Recurse -Force }
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

$pristineDir = Join-Path $workRoot 'pristine'
New-Item -ItemType Directory -Path $pristineDir -Force | Out-Null
Push-Location $pristineDir
try {
    & $ar x $SourceArchive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "ar x (pristine) failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

$pristineObject = Join-Path $pristineDir $MemberName
if (-not (Test-Path -LiteralPath $pristineObject)) { throw "Archive does not contain $MemberName" }

$patchDir = Join-Path $workRoot 'patch'
New-Item -ItemType Directory -Path $patchDir -Force | Out-Null
$objectPath = Join-Path $patchDir $MemberName
Copy-Item -LiteralPath $pristineObject -Destination $objectPath -Force

$objectSha = Get-Sha256 $objectPath
Write-Step "Member sha256 : $objectSha"

$variant = $Variants | Where-Object { $_.MemberSha -eq $objectSha } | Select-Object -First 1
if (-not $variant) {
    # Fall back to word matching so an already patched or slightly rebuilt member is still recognised.
    $bytes = [System.IO.File]::ReadAllBytes($objectPath)
    foreach ($candidate in $Variants) {
        $match = $true
        foreach ($patch in $candidate.Patches) {
            $word = [BitConverter]::ToUInt32($bytes, $patch.Offset)
            if ($word -ne $patch.Old -and $word -ne $patch.New) { $match = $false; break }
        }
        if ($match) { $variant = $candidate; break }
    }
    if (-not $variant) {
        throw "Unsupported $MemberName (sha $objectSha). Re-analyse the offsets before patching."
    }
    Write-Warning "Member hash is unknown; matched variant by instruction words: $($variant.Name)"
}
Write-Step "Variant: $($variant.Name)"
$MemberPatches = $variant.Patches

# --- 2. Patch the member ---------------------------------------------------
$bytes = [System.IO.File]::ReadAllBytes($objectPath)
$alreadyPatched = $true
foreach ($patch in $MemberPatches) {
    if ([BitConverter]::ToUInt32($bytes, $patch.Offset) -ne $patch.New) { $alreadyPatched = $false }
}

if ($alreadyPatched) {
    Write-Step 'Member is already patched; keeping the current bytes.'
}
else {
    Write-Step 'Applying instruction patches'
    foreach ($patch in $MemberPatches) {
        $word = [BitConverter]::ToUInt32($bytes, $patch.Offset)
        if ($word -ne $patch.Old) {
            if (-not $Force) {
                throw ("Unexpected instruction at 0x{0:X4}: 0x{1:X8} (expected 0x{2:X8})" -f $patch.Offset, $word, $patch.Old)
            }
            Write-Warning ("0x{0:X4}: 0x{1:X8} does not match 0x{2:X8}; overwriting anyway" -f $patch.Offset, $word, $patch.Old)
        }
        $newBytes = [BitConverter]::GetBytes([uint32]$patch.New)
        [Array]::Copy($newBytes, 0, $bytes, $patch.Offset, 4)
        Write-Host ("    0x{0:X4}: 0x{1:X8} -> 0x{2:X8}  {3}" -f $patch.Offset, $patch.Old, $patch.New, $patch.Name)
    }
    [System.IO.File]::WriteAllBytes($objectPath, $bytes)
}

$verifyBytes = [System.IO.File]::ReadAllBytes($objectPath)
foreach ($patch in $MemberPatches) {
    if ([BitConverter]::ToUInt32($verifyBytes, $patch.Offset) -ne $patch.New) {
        throw ("Patch verification failed at 0x{0:X4}" -f $patch.Offset)
    }
}

$patchedObjectSha = Get-Sha256 $objectPath
Write-Step "Patched member sha256: $patchedObjectSha"
Copy-Item -LiteralPath $objectPath -Destination (Join-Path $OutDir 'ieee80211_output.patched.o') -Force

# --- 3. Repack the archive -------------------------------------------------
$workArchive = Join-Path $workRoot 'libnet80211.a'
Copy-Item -LiteralPath $SourceArchive -Destination $workArchive -Force

Write-Step 'Repacking libnet80211.a'
Push-Location $patchDir
try {
    & $ar r $workArchive $MemberName | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "ar r failed with exit code $LASTEXITCODE" }
    & $ranlib $workArchive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "ranlib failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

# --- 4. Verify the rebuilt archive ----------------------------------------
$afterDir = Join-Path $workRoot 'after'
New-Item -ItemType Directory -Path $afterDir -Force | Out-Null
Push-Location $afterDir
try {
    & $ar x $workArchive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "ar x (after) failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

$rebuiltMemberSha = Get-Sha256 (Join-Path $afterDir $MemberName)
if ($rebuiltMemberSha -ne $patchedObjectSha) { throw "Rebuilt member hash mismatch: $rebuiltMemberSha" }

$beforeFiles = Get-ChildItem -LiteralPath $pristineDir -File | Sort-Object Name
$afterFiles  = Get-ChildItem -LiteralPath $afterDir  -File | Sort-Object Name
if ($beforeFiles.Count -ne $afterFiles.Count) { throw "Member count changed: $($beforeFiles.Count) -> $($afterFiles.Count)" }

$changed = 0
foreach ($before in $beforeFiles) {
    $after = Join-Path $afterDir $before.Name
    if (-not (Test-Path -LiteralPath $after)) { throw "Member missing after repack: $($before.Name)" }
    $beforeSha = Get-Sha256 $before.FullName
    $afterSha  = Get-Sha256 $after
    if ($beforeSha -ne $afterSha) {
        if ($before.Name -ne $MemberName) { throw "Unexpected change in member $($before.Name)" }
        $changed++
    }
}
Write-Step "Archive integrity: $($beforeFiles.Count) members, changed: $changed (expected 1)"

$patchedArchiveOut = Join-Path $OutDir 'libnet80211.patched.a'
Copy-Item -LiteralPath $workArchive -Destination $patchedArchiveOut -Force
$patchedArchiveSha = Get-Sha256 $patchedArchiveOut
Write-Step "Patched archive: $patchedArchiveOut"
Write-Step "Patched archive sha256: $patchedArchiveSha"

# --- 5. Install into PlatformIO (optional) --------------------------------
if ($Install) {
    Write-Step 'Installing patched archive into PlatformIO'
    if (-not (Test-Path -LiteralPath $BackupDir)) { New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null }

    $backupNextToOriginal = "$SourceArchive.orig-backup"
    $backupInProject      = Join-Path $BackupDir 'libnet80211.a.orig'

    $currentSha = Get-Sha256 $SourceArchive
    if ($currentSha -eq $patchedArchiveSha) {
        if (-not (Test-Path -LiteralPath $backupNextToOriginal)) {
            throw "Installed archive is already patched but no backup exists: $backupNextToOriginal"
        }
        if (-not (Test-Path -LiteralPath $backupInProject)) {
            Copy-Item -LiteralPath $backupNextToOriginal -Destination $backupInProject -Force
        }
        Write-Step 'Installed archive is already patched; backups kept.'
    }
    else {
        if (-not (Test-Path -LiteralPath $backupNextToOriginal)) {
            Copy-Item -LiteralPath $SourceArchive -Destination $backupNextToOriginal -Force
            Write-Step "Backup created: $backupNextToOriginal"
        }
        elseif ((Get-Sha256 $backupNextToOriginal) -ne $currentSha) {
            throw "Existing backup does not match the current archive: $backupNextToOriginal"
        }
        Copy-Item -LiteralPath $SourceArchive -Destination $backupInProject -Force
        Write-Step "Backup created: $backupInProject"

        Copy-Item -LiteralPath $patchedArchiveOut -Destination $SourceArchive -Force
        $installedSha = Get-Sha256 $SourceArchive
        if ($installedSha -ne $patchedArchiveSha) { throw "Installed archive hash mismatch: $installedSha" }
        Write-Step "Installed: $SourceArchive"
        Write-Step "Installed sha256: $installedSha"
    }
}

Write-Step 'Done.'
