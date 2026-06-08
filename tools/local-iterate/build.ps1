# Local Switch-build iteration: per-build script.
#
# Re-applies all the CI vcxproj/header patches (idempotent), then runs
# msbuild on Solar2D.sln. Run this after each edit to .github/workflows/
# patch logic OR after pulling fresh librtt changes.
#
# Prereqs:
#   - tools\local-iterate\setup-once.ps1 has been run successfully
#   - C:\Nintendo\NativeSDK22.2.0\NintendoSDK exists (auto-detected if elsewhere)
#   - Visual Studio 2022 with NX64 platform extension installed
#
# Usage:  powershell -File tools\local-iterate\build.ps1 [-Config Release|Develop|Debug]
# Default config: Release (matches CI).

param(
    [ValidateSet('Release','Develop','Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")
$root = Get-Location
Write-Host "Repo root: $root" -ForegroundColor Cyan
Write-Host "Config:    $Config|NX64" -ForegroundColor Cyan

# ============================================================================
# Locate Nintendo SDK
# ============================================================================
$candidates = @(
    "D:\Nintendo\Solar2D\NintendoSDK",     # matches CI layout
    "C:\Nintendo\Solar2D\NintendoSDK",
    "C:\Nintendo\NativeSDK22.2.0\NintendoSDK"
)
$sdk = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sdk) { throw "NintendoSDK not found. Tried: $($candidates -join ', ')" }
$env:NINTENDO_SDK_ROOT = $sdk
Write-Host "SDK:       $sdk" -ForegroundColor Cyan

# ============================================================================
# Locate MSBuild via vswhere
# ============================================================================
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe not found via vswhere" }
Write-Host "MSBuild:   $msbuild" -ForegroundColor Cyan

# ============================================================================
# Patch memoryBitmap.vcxproj (inject librtt include path)
# ============================================================================
Write-Host "`n[patch] memoryBitmap.vcxproj..." -ForegroundColor Green
$mbPath = "platform\switch\plugins\memoryBitmap\memoryBitmap.vcxproj"
$mb = Get-Content $mbPath -Raw
if ($mb -notmatch 'librtt\\Corona') {
    $mb = $mb -replace `
        '<AdditionalIncludeDirectories>(\.\.\\\.\.\\\.\.\\external\\lua-5\.1\.3\\src)</AdditionalIncludeDirectories>', `
        '<AdditionalIncludeDirectories>..\..\..\..\librtt\Corona;..\..\..\..\librtt;$1</AdditionalIncludeDirectories>'
    Set-Content -Path $mbPath -Value $mb -NoNewline
    Write-Host "  injected"
} else {
    Write-Host "  already patched"
}

# ============================================================================
# Patch Solar2D.vcxproj
#
# The Switch port vcxproj doesn't list a handful of librtt sources that
# surviving librtt code (Rtt_LuaLibDisplay, Rtt_SpriteObject,
# Rtt_PlatformInputDevice, Rtt_PlatformOpenALPlayer) calls into. Inject
# them right after Rtt_Runtime.cpp.
# ============================================================================
Write-Host "`n[patch] Solar2D.vcxproj..." -ForegroundColor Green
$sPath = "platform\switch\Solar2D\Solar2D.vcxproj"
$s = Get-Content $sPath -Raw
if ($s -notmatch 'Rtt_ControllerTypeClassifier\.cpp') {
    # Rtt_SpriteSequence.cpp is in the pristine vcxproj; the other three aren't.
    $missing = @(
        '    <ClCompile Include="..\..\..\librtt\Input\Rtt_ControllerTypeClassifier.cpp" />',
        '    <ClCompile Include="..\..\..\librtt\Input\Rtt_GameControllerDB.cpp" />',
        '    <ClCompile Include="..\..\..\librtt\Rtt_PlatformAudioSessionManager.cpp" />'
    ) -join "`r`n"
    $s = $s -replace `
        '(<ClCompile Include="\.\.\\\.\.\\\.\.\\librtt\\Rtt_Runtime\.cpp"\s*/>)', `
        "`$1`r`n$missing"
    Set-Content -Path $sPath -Value $s -NoNewline
    Write-Host "  injected 3 ClCompile entries"
} else {
    Write-Host "  ClCompile entries already present"
}
if ((Get-Content $sPath -Raw) -notmatch 'Rtt_ControllerTypeClassifier\.cpp') { throw "ControllerTypeClassifier not injected" }
if ((Get-Content $sPath -Raw) -notmatch 'Rtt_PlatformAudioSessionManager\.cpp') { throw "PlatformAudioSessionManager not injected" }

# ============================================================================
# Generate luaload stubs
#
# Start empty - if linker complains about specific luaload_* symbols not
# being generated from .lua sources, add them here.
# ============================================================================
Write-Host "`n[patch] Generating Rtt_LualoadStubs.cpp..." -ForegroundColor Green
$stubPath = "librtt\Rtt_LualoadStubs.cpp"
$stubFuncs = @()
$stubLines = @(
    '// Auto-generated CI stub: provides no-op definitions for luaload_* symbols',
    '// that the Switch build does not generate from .lua sources.',
    'struct lua_State;',
    'namespace Rtt {'
)
$stubLines += $stubFuncs | ForEach-Object { "  int $_(lua_State*) { return 0; }" }
$stubLines += '}'
$stubContent = $stubLines -join "`r`n"
Set-Content -Path $stubPath -Value $stubContent -NoNewline

# ============================================================================
# Run MSBuild
# ============================================================================
Write-Host "`n[build] Solar2D.sln /p:Configuration=$Config /p:Platform=NX64`n" -ForegroundColor Green
Push-Location platform\switch
try {
    # Kill stuck MSBuild workers (they hold .tlog handles). Wrap in cmd /c so
    # taskkill's stderr noise doesn't trip $ErrorActionPreference='Stop'.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    cmd /c "taskkill /F /IM MSBuild.exe >nul 2>nul"
    $ErrorActionPreference = $prevEAP

    # Restore. Native commands: silence stderr with 2>&1 then filter -- otherwise
    # MSBuild warnings on stderr trip the Stop preference.
    & $msbuild Solar2D.sln /t:Restore /p:Configuration=$Config /p:Platform=NX64 /p:PlatformToolset=v143 /v:minimal /nologo 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "Restore failed ($LASTEXITCODE)" }

    # Build.
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $msbuild Solar2D.sln /maxcpucount /nodeReuse:false /t:Build /p:Configuration=$Config /p:Platform=NX64 /p:PlatformToolset=v143 /v:minimal /nologo /fl /flp:"logfile=msbuild.log;verbosity=normal" 2>&1 | ForEach-Object { "$_" }
    $exit = $LASTEXITCODE
    $sw.Stop()
    Write-Host "`nBuild time: $([math]::Round($sw.Elapsed.TotalSeconds,1))s  -  exit $exit" -ForegroundColor Cyan
    if ($exit -ne 0) {
        Write-Host "`nFirst 30 lines of msbuild.log around 'error':" -ForegroundColor Yellow
        Select-String -Path msbuild.log -Pattern 'error|fatal' -SimpleMatch | Select-Object -First 30 | ForEach-Object { Write-Host $_.Line }
        Write-Host "`nFull log: platform\switch\msbuild.log" -ForegroundColor Yellow
        exit $exit
    }
    Write-Host "`n[OK] Build succeeded." -ForegroundColor Green
} finally {
    Pop-Location
}
