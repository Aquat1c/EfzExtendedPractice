param(
    [ValidateSet("default", "xp")]
    [string]$Target = "default"
)

$ErrorActionPreference = "Stop"

function Get-ProjectRoot {
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
}

function Test-VsMajorInstalled {
    param([int]$Major)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    # Without vswhere nothing can be ruled out; CMake reports a missing generator itself.
    if (-not (Test-Path $vswhere)) {
        return $true
    }

    $range = "[$Major.0,$($Major + 1).0)"
    return [bool](& $vswhere -products * -prerelease -version $range -property installationPath)
}

function Get-BuildConfig {
    param(
        [string]$BuildTarget,
        [string]$CMakeHelpText
    )

    # CMake lists every generator it knows, installed or not.
    if ($CMakeHelpText -match "Visual Studio 18 2026" -and (Test-VsMajorInstalled -Major 18)) {
        if ($BuildTarget -eq "xp") {
            return @{
                ConfigurePreset = "vs18-xp-win32"
                BuildPreset = "vs18-xp-release"
                ExpectedGenerator = "Visual Studio 18 2026"
                ExpectedToolset = "v141_xp"
                BuildDirName = "build_xp_win32"
            }
        }

        return @{
            ConfigurePreset = "vs18-win32"
            BuildPreset = "vs18-release"
            ExpectedGenerator = "Visual Studio 18 2026"
            ExpectedToolset = ""
            BuildDirName = "build"
        }
    }

    if ($CMakeHelpText -match "Visual Studio 17 2022" -and (Test-VsMajorInstalled -Major 17)) {
        if ($BuildTarget -eq "xp") {
            return @{
                ConfigurePreset = "vs17-xp-win32"
                BuildPreset = "vs17-xp-release"
                ExpectedGenerator = "Visual Studio 17 2022"
                ExpectedToolset = "v141_xp"
                BuildDirName = "build_xp_win32_vs17"
            }
        }

        return @{
            ConfigurePreset = "vs17-win32"
            BuildPreset = "vs17-release"
            ExpectedGenerator = "Visual Studio 17 2022"
            ExpectedToolset = ""
            BuildDirName = "build"
        }
    }

    throw "No supported Visual Studio CMake generator found (expected VS18/VS17)."
}

function Reset-StaleCacheIfNeeded {
    param(
        [string]$BuildDir,
        [string]$ProjectRoot,
        [string]$ExpectedGenerator,
        [string]$ExpectedToolset
    )

    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cachePath)) {
        return
    }

    $cachedSource = (Select-String -Path $cachePath -Pattern "^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$" | Select-Object -First 1).Matches.Groups[1].Value
    $cachedGenerator = (Select-String -Path $cachePath -Pattern "^CMAKE_GENERATOR:INTERNAL=(.+)$" | Select-Object -First 1).Matches.Groups[1].Value
    $cachedToolset = (Select-String -Path $cachePath -Pattern "^CMAKE_GENERATOR_TOOLSET:INTERNAL=(.*)$" | Select-Object -First 1).Matches.Groups[1].Value

    $projectRootNorm = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\').ToLowerInvariant()
    $cachedSourceNorm = ""
    if ($cachedSource) {
        $cachedSourceNorm = [System.IO.Path]::GetFullPath($cachedSource).TrimEnd('\').ToLowerInvariant()
    }

    $wrongSource = $cachedSourceNorm -and $cachedSourceNorm -ne $projectRootNorm
    $wrongGenerator = $cachedGenerator -and $cachedGenerator -ne $ExpectedGenerator
    $wrongToolset = $ExpectedToolset -and $cachedToolset -and $cachedToolset -ne $ExpectedToolset
    $missingExpectedToolset = $ExpectedToolset -and -not $cachedToolset

    if (-not ($wrongSource -or $wrongGenerator -or $wrongToolset -or $missingExpectedToolset)) {
        return
    }

    Write-Host "Resetting stale CMake cache for current workspace/generator..." -ForegroundColor Cyan
    Remove-Item $cachePath -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $BuildDir "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue
}

$projectRoot = Get-ProjectRoot
$cmakeHelp = (& cmake --help | Out-String)
$config = Get-BuildConfig -BuildTarget $Target -CMakeHelpText $cmakeHelp
$buildDir = Join-Path $projectRoot $config.BuildDirName

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Reset-StaleCacheIfNeeded -BuildDir $buildDir `
    -ProjectRoot $projectRoot `
    -ExpectedGenerator $config.ExpectedGenerator `
    -ExpectedToolset $config.ExpectedToolset

Write-Host ("Using CMake presets: {0} / {1}" -f $config.ConfigurePreset, $config.BuildPreset) -ForegroundColor Cyan
cmake --preset $config.ConfigurePreset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset $config.BuildPreset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dllOutputPath = Join-Path $buildDir "bin\\Release\\efz_training_mode.dll"
if (Test-Path $dllOutputPath) {
    Write-Host ("Build completed successfully: {0}" -f $dllOutputPath) -ForegroundColor Cyan
} else {
    Write-Host ("Build completed but DLL not found at expected location: {0}" -f $dllOutputPath) -ForegroundColor Yellow
}
