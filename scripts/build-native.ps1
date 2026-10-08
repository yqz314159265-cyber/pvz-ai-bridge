[CmdletBinding()]
param(
    [string] $VisualStudioRoot,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$nativeSource = Join-Path $projectRoot 'native'
$buildDir = Join-Path $projectRoot 'build\native'
if (-not $VisualStudioRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ tools or pass -VisualStudioRoot.' }
    $VisualStudioRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioRoot) { throw 'No Visual Studio installation with x86 C++ tools was found.' }
}
$cmake = Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "Bundled CMake was not found: $cmake"
}
if (-not (Test-Path -LiteralPath (Join-Path $nativeSource 'CMakeLists.txt') -PathType Leaf)) {
    throw "Native CMake project was not found: $nativeSource"
}

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
    if ($resolvedBuild -ne $buildDir -or -not $resolvedBuild.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build output outside the project: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$installationMajor = (Get-Item -LiteralPath (Join-Path $VisualStudioRoot 'MSBuild\Current\Bin\MSBuild.exe')).VersionInfo.FileMajorPart
$generator = switch ($installationMajor) { 18 { 'Visual Studio 18 2026' } 17 { 'Visual Studio 17 2022' } default { throw 'Visual Studio 2022 or 2026 is required.' } }
& $cmake -S $nativeSource -B $buildDir -G $generator -A Win32
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }
& $cmake --build $buildDir --config Release --target pvz_bridge pvz_injector --parallel
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE." }

$expected = @(
    (Join-Path $buildDir 'Release\pvz_bridge.dll'),
    (Join-Path $buildDir 'Release\pvz_injector.exe')
)
foreach ($artifact in $expected) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Expected build artifact is missing: $artifact"
    }
    Write-Output $artifact
}
