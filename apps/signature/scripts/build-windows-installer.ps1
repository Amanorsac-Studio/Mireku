# build-windows-installer.ps1 - builds Mireku-<version>-Windows.exe.
#
#   powershell -ExecutionPolicy Bypass -File apps\signature\scripts\build-windows-installer.ps1
#   ... -Tester                     a TEST build: asks for a key, named Mireku-<v>-Tester-Windows.exe
#   ... -CertThumbprint <sha1>      sign with a certificate in the Windows store
#   ... -SkipBuild                  package what is already built
#   ... -SkipTests                  do not run ctest first
#
# A test build is made in its own folder (build-tester) so it can never be
# mistaken for, or overwrite, the release binaries. It carries the key check in
# src\plugin\TesterGate.h and MUST NOT be sold; see that file for what it is.
#
# In order: builds Release, runs the tests, checks that a RELEASE binary carries
# no signing key and a TESTER binary does, signs every binary if a certificate
# was named, compiles installer\Mireku.iss, and prints the filename, size and
# SHA-256 that go in the delivery note.
#
# SIGNING. No secret is read, stored or passed by this script. -CertThumbprint
# names a certificate already in the Windows certificate store (or on a hardware
# token); signtool asks the store for it. Without it the installer is built
# UNSIGNED and the script says so in capitals, because an unsigned installer is
# not a release.
param(
    [string] $CertThumbprint = "",
    [string] $TimestampUrl   = "http://timestamp.digicert.com",
    [switch] $SkipBuild,
    [switch] $SkipTests,
    [switch] $Tester
)
$ErrorActionPreference = "Stop"
$app   = Split-Path -Parent $PSScriptRoot
$build = Join-Path $app $(if ($Tester) { "build-tester" } else { "build" })
$rel   = Join-Path $build "Mireku_artefacts\Release"
$out   = Join-Path $app "release"

function Find-Tool([string[]] $candidates, [string] $what) {
    foreach ($c in $candidates) {
        if (-not $c) { continue }
        $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -Last 1
        if ($hit) { return $hit.FullName }
    }
    throw "Could not find $what."
}
# Visual Studio: whichever edition vswhere reports (Community here, Enterprise on
# a GitHub runner), falling back to the studio machine's own install.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $(if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1 })
if (-not $vs) { $vs = "C:\Program Files\Microsoft Visual Studio\18\Community" }
$onPath = { param($n) (Get-Command $n -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
$cmake  = Find-Tool @("$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe", (& $onPath "cmake.exe")) "cmake"
$ctest  = Find-Tool @("$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe", (& $onPath "ctest.exe")) "ctest"
$crt    = Find-Tool @("$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT") "the Visual C++ runtime DLLs"
$iscc   = Find-Tool @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "C:\Program Files (x86)\Inno Setup 6\ISCC.exe", (& $onPath "ISCC.exe")) "Inno Setup (ISCC.exe)"

# ---- version: one string, read from the one place it is defined
$version = (Select-String -Path (Join-Path $app "CMakeLists.txt") -Pattern 'project\(Mireku VERSION ([0-9]+\.[0-9]+\.[0-9]+)').Matches[0].Groups[1].Value
foreach ($f in @("installer\Mireku.iss", "installer\README.txt", "installer\README-tester.txt")) {
    if (-not (Select-String -Path (Join-Path $app $f) -SimpleMatch $version -Quiet)) {
        throw "$f does not carry version $version."
    }
}
Write-Host "MIREKU $version $(if ($Tester) { '(TEST BUILD)' })" -ForegroundColor Cyan

# ---- build
if (-not $SkipBuild) {
    # Every path here contains a space ("C:\Amanorsac Studio\..."), so each one is
    # quoted individually. Joining the list unquoted split the source directory at
    # the space and cmake looked for "C:/Amanorsac".
    # `$args` is a PowerShell automatic variable, so the list is named $cfg.
    # Every installer is a PLAYER build (2026-11-04): release mode pinned, no
    # authoring tools, the library sealed. The studio's own build stays OFF.
    $cfg = "-S `"$app`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMIREKU_PLAYER_BUILD=ON"
    # A RELEASE is the licensed product (License Integration Standard 1.1); a
    # -Tester package is opened by its test key instead. Never both (R2), and each
    # is stated explicitly so a build folder reused for the other kind is corrected.
    if ($Tester) { $cfg += " -DMIREKU_TESTER_BUILD=ON -DMIREKU_LICENSED_BUILD=OFF" }
    else         { $cfg += " -DMIREKU_TESTER_BUILD=OFF -DMIREKU_LICENSED_BUILD=ON" }
    $vcvars = "$vs\VC\Auxiliary\Build\vcvars64.bat"
    & cmd /c "`"$vcvars`" >nul && `"$cmake`" $cfg"
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    & cmd /c "`"$vcvars`" >nul && `"$cmake`" --build `"$build`" --config Release"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not $SkipTests) {
    & $ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "tests failed" }
}

# ---- the key check, both ways round.
#
# A RELEASE must not contain the test-key modulus: it is the public half, but the
# License Integration Standard's R2 forbids a second signing key in a release at
# all. A TESTER must contain it, or the build was configured without
# MIREKU_TESTER_BUILD and would ship with no gate while being named as gated -
# the more dangerous of the two mistakes, so it is checked too.
$keyHeader = Join-Path $app "src\plugin\TesterKey.h"
$modulus = ([regex]::Match((Get-Content $keyHeader -Raw), '"([0-9a-f]{64})"')).Groups[1].Value
if (-not $modulus) { throw "Could not read the public modulus from $keyHeader." }

# The STUDIO key (engine/Sealed.h): a binary compiled without it carries the
# all-zero TEST key and can open no sealed library, so it must never be packaged.
# The configure writes generated\StudioKey.h and says which key went in.
$studioKey = Join-Path $build "generated\StudioKey.h"
if (-not (Test-Path $studioKey)) { throw "No $studioKey - the configure did not run gen-studio-key.js." }
if ((Get-Content $studioKey -Raw) -match 'kIsTestKey = true') {
    throw "REFUSED: the binary was built with the TEST studio key (no ~\.amanorsac\studio-keys.json or AOFAS_STUDIO_KEY). It could not open the sealed library."
}
if (-not ((Get-Content $studioKey -Raw) -match 'MIREKU_PLAYER_BUILD') -and -not (Select-String -Path (Join-Path $build "CMakeCache.txt") -Pattern 'MIREKU_PLAYER_BUILD:BOOL=ON' -Quiet)) {
    throw "REFUSED: $build was not configured with MIREKU_PLAYER_BUILD=ON. Delete the build folder and run again."
}
Write-Host "  studio key: compiled in (not the test key); player build: yes" -ForegroundColor Green
$exe = Join-Path $rel "Standalone\MIREKU.exe"
if (-not (Test-Path $exe)) { throw "No standalone at $exe - build first." }

# Inno does NOT fail on a wildcard that matches nothing, so an empty VST3 folder
# compiles cleanly into an installer whose "VST3 plug-in" checkbox installs
# nothing at all. Checked here instead, because a missing plug-in is not
# something the person installing it would notice until a DAW failed to find it.
$vst3 = Join-Path $rel "VST3\MIREKU.vst3"
$vst3Binary = Join-Path $vst3 "Contents\x86_64-win\MIREKU.vst3"
if (-not (Test-Path $vst3Binary)) {
    throw "No VST3 at $vst3Binary - the bundle is missing or empty, so the installer would carry no plug-in. Build the Mireku_VST3 target."
}
$bytes = [System.IO.File]::ReadAllBytes($exe)
$text  = [System.Text.Encoding]::ASCII.GetString($bytes)
$carries = $text.Contains($modulus)
if ($Tester -and -not $carries) {
    throw "This is a -Tester package but the binary carries no key: it was built without MIREKU_TESTER_BUILD and has NO GATE."
}
if (-not $Tester -and $carries) {
    throw "RELEASE REFUSED: the binary contains the test-key modulus. Build the release in a clean folder without MIREKU_TESTER_BUILD."
}
Write-Host "  key check: $(if ($Tester) { 'tester binary carries the key, as it must' } else { 'release binary carries no key' })" -ForegroundColor Green

# ---- the licence check (Licence Standard acceptance A11 and A12), in the bytes
# of BOTH shipped binaries. A release must be gated, must carry the studio public
# key exactly once, and must name the live server. A -Tester package is not
# licensed and is not checked.
if (-not $Tester) {
    if (-not (Select-String -Path (Join-Path $build "CMakeCache.txt") -Pattern 'MIREKU_LICENSED_BUILD:BOOL=ON' -Quiet)) {
        throw "RELEASE REFUSED: $build was not configured with MIREKU_LICENSED_BUILD=ON, so it has no licence gate."
    }
    $latin = [System.Text.Encoding]::GetEncoding(28591)
    $needle = $latin.GetString([byte[]](0xcd,0xa5,0x7d,0x1c,0xc8,0xa6,0xe2,0x71))
    foreach ($bin in @($exe, $vst3Binary)) {
        $raw = $latin.GetString([System.IO.File]::ReadAllBytes($bin))
        $hits = ([regex]::Matches($raw, [regex]::Escape($needle))).Count
        if ($hits -ne 1) { throw "RELEASE REFUSED (A11): $bin contains the studio licence key $hits time(s); exactly 1 is required." }
        if (-not $raw.Contains("https://amanorsac.studio")) { throw "RELEASE REFUSED (A12): $bin does not name https://amanorsac.studio." }
        if ($raw.Contains("127.0.0.1")) { throw "RELEASE REFUSED (A12): $bin contains 127.0.0.1." }
    }
    Write-Host "  licence check: gated; studio key present once; server https://amanorsac.studio (A11, A12)" -ForegroundColor Green
}

# ---- sign
$signCmd = ""
if ($CertThumbprint) {
    $signtool = Find-Tool @("${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe", (& $onPath "signtool.exe")) "signtool"
    $signCmd = "`"$signtool`" sign /sha1 $CertThumbprint /fd sha256 /tr $TimestampUrl /td sha256 `$f"
} else {
    Write-Host "  NOT SIGNED - no -CertThumbprint given. This is not a release." -ForegroundColor Yellow
}

# ---- compile the installer
New-Item -ItemType Directory -Force -Path $out | Out-Null
$issArgs = @("/DBuildDir=$rel", "/DCrtDir=$crt", "/DOutDir=$out")
if ($Tester)  { $issArgs += "/DTester" }
if ($signCmd) { $issArgs += "/DSignCmd=$signCmd" }
$issArgs += (Join-Path $app "installer\Mireku.iss")
& $iscc @issArgs
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }

# ---- the delivery note
$tag = $(if ($Tester) { "-Tester" } else { "" })
$pkg = Join-Path $out "Mireku-$version$tag-Windows.exe"
$item = Get-Item $pkg
Write-Host ""
Write-Host "  $($item.Name)" -ForegroundColor Cyan
Write-Host "  $([math]::Round($item.Length / 1MB, 1)) MB"
Write-Host "  SHA-256 $((Get-FileHash $pkg -Algorithm SHA256).Hash.ToLower())"
