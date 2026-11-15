# Milestone 13: builds BOUND for Windows from this checkout and packs it the way a release ships it:
#
#   dist\bound-<version>-win64.zip    a folder, bound-<version>, holding bound-qt.exe, boundd.exe,
#                                     bound-cli.exe, bound-explorer.exe, bound-seeder.exe and every
#                                     DLL they need, so it runs on a PC with nothing installed
#
# Run it at the repository's root, from Developer PowerShell for VS 2022 (the release workflow does
# the same on GitHub):
#
#   powershell -ExecutionPolicy Bypass -File contrib\release\package-windows.ps1
#
# It builds Release in build\package (with the wallet app, so vcpkg needs Qt: about an hour the first
# time), runs every test on those exact programs, installs them into dist\bound-<version>, checks
# they run from there with nothing else on PATH, and zips the folder.
param(
    [string]$OutDir = "dist",
    [string]$BuildDir = "build\package",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3

# Runs a program and stops the script if it fails. PowerShell 5.1 under "Stop" can also stop at the
# first line a program writes to stderr (cmake's warnings, say), so that's relaxed around the call.
function Invoke-Checked {
    # @(...): splatting a lone string would pass it one character at a time.
    $program = $args[0]
    $arguments = @($args | Select-Object -Skip 1)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $program @arguments
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) { throw "failed (exit code $LASTEXITCODE): $program $arguments" }
}

if (-not (Test-Path CMakeLists.txt) -or -not (Test-Path contrib\release)) { throw "run this at the repository's root" }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw "run this from Developer PowerShell for VS 2022" }
if (-not $env:VCPKG_ROOT) { throw "set VCPKG_ROOT to vcpkg (Developer PowerShell for VS 2022 sets it)" }

Write-Host "== Configuring $BuildDir"
Invoke-Checked cmake --preset msvc -B $BuildDir -DBOUND_GUI=ON
Write-Host "== Building (Release)"
Invoke-Checked cmake --build $BuildDir --config Release
if (-not $SkipTests) {
    Write-Host "== Testing the programs about to be packed"
    Invoke-Checked ctest --test-dir $BuildDir -C Release --output-on-failure
}

$version = (Select-String -Path "$BuildDir\CMakeCache.txt" -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$').Matches[0].Groups[1].Value
$name = "bound-$version"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }

Write-Host "== Installing into $stage"
Invoke-Checked cmake --install $BuildDir --config Release --prefix $stage
Copy-Item contrib\release\README.txt $stage
# Qt is under the LGPL: say which Qt this is and where its source is, as vcpkg built it.
$qt = (Get-Item "$stage\Qt6Core.dll").VersionInfo.ProductVersion
$baseline = (Get-Content -Raw vcpkg-configuration.json | ConvertFrom-Json).'default-registry'.baseline
$qtSeries = ($qt -split '\.')[0..1] -join '.'
Add-Content -Encoding ascii -Path "$stage\README.txt" -Value @"

Qt
  This package carries Qt $qt (Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, platforms\qwindows.dll),
  under the GNU LGPL version 3: see licenses\qtbase.txt. You may replace those DLLs with your own
  build of Qt $qtSeries. Qt's source: https://download.qt.io/archive/qt/$qtSeries/$qt/submodules/
  and vcpkg's build of it: https://github.com/microsoft/vcpkg/tree/$baseline/ports/qtbase
"@

Write-Host "== Checking the package runs on its own"
$programs = "boundd", "bound-cli", "bound-explorer", "bound-seeder"
$savedPath = $env:PATH
# Nothing from the build or vcpkg: whatever the programs need has to be in the folder.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
$data = Join-Path ([IO.Path]::GetTempPath()) "bound-package-check-$PID"
try {
    foreach ($program in $programs) {
        $line = @(Invoke-Checked "$stage\$program.exe" --version)[0]
        if (-not "$line".StartsWith("$program $version")) { throw "$program --version said: $line" }
        Write-Host "   $line"
    }
    Invoke-Checked "$stage\boundd.exe" --regtest demo | Out-Null
    Write-Host "   boundd demo: mined, paid, reorganised"

    # The wallet app on regtest, in a data directory of its own: its window has to open (so Qt's DLLs
    # and platform plugin are all there), its node has to start and answer bound-cli, and stopping
    # the node has to close it.
    function Invoke-Cli {
        $saved = $ErrorActionPreference
        $ErrorActionPreference = "SilentlyContinue"
        try {
            $out = & "$stage\bound-cli.exe" --regtest --datadir $data @args 2>$null
        } finally {
            $ErrorActionPreference = $saved
        }
        if ($LASTEXITCODE -ne 0) { return $null }
        return ($out -join "`n")
    }
    $app = Start-Process -FilePath "$stage\bound-qt.exe" -ArgumentList "--regtest", "--datadir", "`"$data`"", "--nolisten" -PassThru
    $null = $app.Handle  # without holding the handle, PowerShell 5.1 can't read the exit code later
    try {
        $chain = $null
        for ($i = 0; $i -lt 90 -and -not $chain; $i++) {
            Start-Sleep -Seconds 1
            if ($app.HasExited) { throw "bound-qt quit at once (exit code $($app.ExitCode)): is a DLL or plugin missing?" }
            $chain = Invoke-Cli getblockchaininfo
        }
        if (-not $chain) { throw "bound-qt's node never answered" }
        if (($chain | ConvertFrom-Json).chain -ne "regtest") { throw "bound-qt runs the wrong chain: $chain" }
        $agent = (Invoke-Cli getnetworkinfo | ConvertFrom-Json).subversion
        if ($agent -ne "/boundd:$version/") { throw "bound-qt's node calls itself $agent" }
        Write-Host "   bound-qt: window open, node running regtest as $agent"
        Invoke-Cli stop | Out-Null
        if (-not $app.WaitForExit(60000)) { throw "bound-qt didn't close after its node stopped" }
        Write-Host "   bound-qt: closed when its node stopped"
    } finally {
        if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
    }
} finally {
    $env:PATH = $savedPath
    Start-Sleep -Seconds 1
    if (Test-Path $data) { Remove-Item -Recurse -Force $data }
}

Write-Host "== Zipping"
$zip = "$name-win64.zip"
Push-Location $OutDir
try {
    if (Test-Path $zip) { Remove-Item $zip }
    Invoke-Checked cmake -E tar cf $zip --format=zip $name
} finally {
    Pop-Location
}
$hash = (Get-FileHash (Join-Path $OutDir $zip) -Algorithm SHA256).Hash.ToLower()
Write-Host "$hash  $zip"
