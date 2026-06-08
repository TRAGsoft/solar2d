# Local Switch-build iteration: one-time setup.
#
# Runs the heavy/idempotent steps so they don't repeat on every build:
#   1. Apply librtt header patches (same as build_test_nintendo.yml source-code job)
#   2. Apply Switch port header patches (override keywords, etc.)
#   3. Clone utf8/memoryBitmap plugin sources (download_plugins.cmd equivalent)
#   4. Create NTFS junctions for buggy vcxproj relative paths
#
# Safe to re-run: every step is idempotent.
#
# Prereqs: git, Git Bash (provides sed) in PATH or at C:\Program Files\Git\.
# Run from repo root: powershell -File tools\local-iterate\setup-once.ps1

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")
Write-Host "Repo root: $(Get-Location)" -ForegroundColor Cyan

# Locate bash.exe (Git Bash)  -  needed for the sed patches.
$bash = $null
foreach ($candidate in @("C:\Program Files\Git\bin\bash.exe",
                          "C:\Program Files (x86)\Git\bin\bash.exe",
                          "$env:ProgramFiles\Git\bin\bash.exe")) {
    if (Test-Path $candidate) { $bash = $candidate; break }
}
if (-not $bash) { $bash = (Get-Command bash.exe -ErrorAction SilentlyContinue).Source }
if (-not $bash) { throw "Could not find bash.exe (Git Bash). Install Git for Windows." }
Write-Host "bash: $bash" -ForegroundColor Cyan

function Run-Bash([string]$Script) {
    # Pipe script via stdin so we don't have to worry about quoting.
    $Script | & $bash --noprofile --norc -s
    if ($LASTEXITCODE -ne 0) { throw "bash step exited $LASTEXITCODE" }
}

# ============================================================================
# 1. Switch port header patches
#
# librtt header patches that used to live here were a workaround for an old
# fat-engine state. Lean librtt does not need them. If the build later
# surfaces specific librtt-API mismatches, add targeted patches here.
# ============================================================================
Write-Host "`n[1/4] Switch port header patches..." -ForegroundColor Green
if (-not (Test-Path "platform\switch\Solar2D\Rtt_NintendoInputDevice.h")) {
    throw "platform/switch not populated. Clone pouwelsjochem/solar2d-platform-switch into platform/switch first."
}
if ((Get-Content "platform\switch\Solar2D\Rtt_NintendoInputDevice.h" -Raw) -match 'GetVendorId\(\);[^o]') {
    Write-Host "  Already patched (override stripped from GetVendorId). Skipping."
} else {
    Run-Bash @'
set -ex
sed -i \
  -e 's|virtual U16 GetVendorId() override;|virtual U16 GetVendorId();|' \
  -e 's|virtual U16 GetProductId() override;|virtual U16 GetProductId();|' \
  platform/switch/Solar2D/Rtt_NintendoInputDevice.h
sed -i \
  -e 's|virtual RenderingStream\* CreateRenderingStream() const override;|virtual RenderingStream* CreateRenderingStream() const;|' \
  -e 's|virtual void SaveBitmap(PlatformBitmap\* bitmap, Rtt::Data<const char> \& pngBytes) const override;|virtual void SaveBitmap(PlatformBitmap* bitmap, Rtt::Data<const char> \& pngBytes) const;|' \
  platform/switch/Solar2D/Rtt_NintendoPlatform.h
echo "GetVendorId(); count:       $(grep -c 'GetVendorId();' platform/switch/Solar2D/Rtt_NintendoInputDevice.h)"
echo "CreateRenderingStream const: $(grep -cE 'CreateRenderingStream\(\) const;$' platform/switch/Solar2D/Rtt_NintendoPlatform.h)"
'@
}

# ============================================================================
# 3. Plugin sources
# ============================================================================
Write-Host "`n[2/4] Plugin sources (utf8, memoryBitmap)..." -ForegroundColor Green
$pluginsDir = "platform\switch\plugins"
if (Test-Path "$pluginsDir\utf8\plugin\.git") {
    Write-Host "  utf8 plugin already cloned. Skipping."
} else {
    git clone https://github.com/coronalabs/com.coronalabs-plugin.utf8.git "$pluginsDir\utf8\plugin"
    if ($LASTEXITCODE -ne 0) { throw "utf8 plugin clone failed" }
}
if (Test-Path "$pluginsDir\memoryBitmap\plugin\.git") {
    Write-Host "  memoryBitmap plugin already cloned. Skipping."
} else {
    git clone https://github.com/coronalabs/com.coronalabs-plugin.memoryBitmap.git "$pluginsDir\memoryBitmap\plugin"
    if ($LASTEXITCODE -ne 0) { throw "memoryBitmap plugin clone failed" }
}

# ============================================================================
# 4. NTFS junctions for buggy vcxproj relative paths
# ============================================================================
Write-Host "`n[3/4] NTFS junctions..." -ForegroundColor Green
# external\mojoAL → platform\switch\mojoAL
if (Test-Path "external\mojoAL") {
    Write-Host "  external\mojoAL already exists. Skipping."
} else {
    cmd /c mklink /J external\mojoAL platform\switch\mojoAL
    if ($LASTEXITCODE -ne 0) { throw "mklink external\mojoAL failed" }
}
# platform\external → external
if (Test-Path "platform\external") {
    Write-Host "  platform\external already exists. Skipping."
} else {
    cmd /c mklink /J platform\external external
    if ($LASTEXITCODE -ne 0) { throw "mklink platform\external failed" }
}

# ============================================================================
# 5. platform\test\assets2 placeholder (Oasis.NX.Deploy.targets junctions to it)
# ============================================================================
Write-Host "`n[4/4] platform\test\assets2 placeholder..." -ForegroundColor Green
if (-not (Test-Path "platform\test\assets2\main.lua")) {
    New-Item -ItemType Directory -Force -Path "platform\test\assets2" | Out-Null
    Set-Content -Path "platform\test\assets2\main.lua" -Value "" -NoNewline
    Write-Host "  created"
} else {
    Write-Host "  already exists"
}

Write-Host "`n[OK] Setup complete. Run tools\local-iterate\build.ps1 to compile." -ForegroundColor Green
