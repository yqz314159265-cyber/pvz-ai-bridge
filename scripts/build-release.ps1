[CmdletBinding()]
param(
    [switch] $SkipNative,
    [switch] $SkipInstaller,
    [switch] $Clean,
    [string] $InnoCompilerPath,
    [string] $PythonExe
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$releaseRoot = Join-Path $projectRoot 'build\release'
$stageRoot = Join-Path $releaseRoot 'dist\pvz-ai'
$workRoot = Join-Path $releaseRoot 'work'
$licenseRoot = Join-Path $releaseRoot 'inputs\licenses'
$python = if ($PythonExe) { $PythonExe } else { Join-Path $projectRoot '.venv\Scripts\python.exe' }

function Assert-WithinProject([string] $Path, [string] $Boundary) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $boundaryPath = [IO.Path]::GetFullPath($Boundary).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($boundaryPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a path outside the release directory: $resolved"
    }
}

if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw "Project Python is missing: $python" }
if ($Clean -and (Test-Path -LiteralPath $releaseRoot)) {
    Assert-WithinProject $releaseRoot (Join-Path $projectRoot 'build')
    $resolvedRelease = (Resolve-Path -LiteralPath $releaseRoot).Path
    if ($resolvedRelease -ne [IO.Path]::GetFullPath($releaseRoot)) { throw "Unexpected release output path: $resolvedRelease" }
    Remove-Item -LiteralPath $releaseRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null

if (-not $SkipNative) {
    & (Join-Path $PSScriptRoot 'build-native.ps1')
    if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE." }
}
$nativeRelease = Join-Path $projectRoot 'build\native\Release'
$nativeDll = Join-Path $nativeRelease 'pvz_bridge.dll'
$nativeInjector = Join-Path $nativeRelease 'pvz_injector.exe'
foreach ($nativeFile in @($nativeDll, $nativeInjector)) {
    if (-not (Test-Path -LiteralPath $nativeFile -PathType Leaf)) {
        throw "Required native release output is missing: $nativeFile"
    }
}

$requiredResources = @(
    'templates', 'skills\pvz-play', 'THIRD_PARTY_NOTICES.md',
    'README.md', 'INTERFACE.md', 'VALIDATION.md',
    'docs\README.en.md', 'docs\README.zh-CN.md', 'docs\AI_GUIDE.md',
    'docs\HUMAN_OPERATIONS.en.md', 'docs\HUMAN_OPERATIONS.zh-CN.md',
    'docs\RELEASE_NOTES.md', 'docs\TEMPLATES.zh-CN.md',
    'docs\LEVEL_CATALOG.zh-CN.md', 'docs\ENTITY_LAYOUT.md',
    'docs\SOURCE_REFERENCES.md'
)
foreach ($resource in $requiredResources) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $resource))) {
        throw "Required release resource is missing: $resource"
    }
}

if (Test-Path -LiteralPath $stageRoot) {
    Assert-WithinProject $stageRoot (Join-Path $projectRoot 'build\release')
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $workRoot) {
    Assert-WithinProject $workRoot (Join-Path $projectRoot 'build\release')
    Remove-Item -LiteralPath $workRoot -Recurse -Force
}
if (Test-Path -LiteralPath $licenseRoot) {
    Assert-WithinProject $licenseRoot (Join-Path $projectRoot 'build\release')
    $resolvedLicenseRoot = (Resolve-Path -LiteralPath $licenseRoot).Path
    if ($resolvedLicenseRoot -ne [IO.Path]::GetFullPath($licenseRoot)) { throw "Unexpected license staging path: $resolvedLicenseRoot" }
    Remove-Item -LiteralPath $resolvedLicenseRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Split-Path $stageRoot -Parent), $workRoot | Out-Null
New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $licenseRoot 'native\minhook'), (Join-Path $licenseRoot 'native\nlohmann') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'native\vendor\minhook\LICENSE.txt') `
    -Destination (Join-Path $licenseRoot 'native\minhook\LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'native\vendor\nlohmann\LICENSE.MIT') `
    -Destination (Join-Path $licenseRoot 'native\nlohmann\LICENSE.MIT')

& $python (Join-Path $projectRoot 'packaging\collect_licenses.py') $licenseRoot
if ($LASTEXITCODE -ne 0) { throw "License collection failed with exit code $LASTEXITCODE." }
$pyInstaller = Join-Path (Split-Path $python -Parent) 'pyinstaller.exe'
if (-not (Test-Path -LiteralPath $pyInstaller -PathType Leaf)) {
    throw "PyInstaller is missing from the project venv. Install it with: `"$python`" -m pip install PyInstaller"
}

$arguments = @(
    '--noconfirm', '--clean', '--onedir', '--console', '--name', 'pvz-ai',
    '--contents-directory', '.', '--paths', (Join-Path $projectRoot 'src'),
    '--distpath', (Join-Path $releaseRoot 'dist'), '--workpath', $workRoot,
    '--specpath', (Join-Path $releaseRoot 'spec'),
    '--add-binary', "$nativeDll;native", '--add-binary', "$nativeInjector;native",
    '--add-data', "$(Join-Path $projectRoot 'templates');templates",
    '--add-data', "$(Join-Path $projectRoot 'skills\pvz-play');skills\pvz-play",
    '--add-data', "$(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md');docs",
    '--add-data', "$licenseRoot;licenses",
    (Join-Path $projectRoot 'packaging\entrypoint.py')
)
foreach ($document in @(
    'README.md', 'INTERFACE.md', 'VALIDATION.md',
    'docs\README.en.md', 'docs\README.zh-CN.md', 'docs\AI_GUIDE.md',
    'docs\HUMAN_OPERATIONS.en.md', 'docs\HUMAN_OPERATIONS.zh-CN.md',
    'docs\RELEASE_NOTES.md', 'docs\TEMPLATES.zh-CN.md',
    'docs\LEVEL_CATALOG.zh-CN.md', 'docs\ENTITY_LAYOUT.md',
    'docs\SOURCE_REFERENCES.md'
)) {
    $destination = if ($document.StartsWith('docs\', [StringComparison]::OrdinalIgnoreCase)) { 'docs' } else { '.' }
    $arguments += @('--add-data', "$(Join-Path $projectRoot $document);$destination")
}
& $pyInstaller @arguments
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed with exit code $LASTEXITCODE." }

$exe = Join-Path $stageRoot 'pvz-ai.exe'
$bundledDll = Join-Path $stageRoot 'native\pvz_bridge.dll'
$bundledInjector = Join-Path $stageRoot 'native\pvz_injector.exe'
foreach ($expected in @($exe, $bundledDll, $bundledInjector,
    (Join-Path $stageRoot 'README.md'),
    (Join-Path $stageRoot 'INTERFACE.md'),
    (Join-Path $stageRoot 'VALIDATION.md'),
    (Join-Path $stageRoot 'templates\illustrative_day_1.json'),
    (Join-Path $stageRoot 'skills\pvz-play'),
    (Join-Path $stageRoot 'docs\README.en.md'),
    (Join-Path $stageRoot 'docs\README.zh-CN.md'),
    (Join-Path $stageRoot 'docs\HUMAN_OPERATIONS.en.md'),
    (Join-Path $stageRoot 'docs\HUMAN_OPERATIONS.zh-CN.md'),
    (Join-Path $stageRoot 'docs\RELEASE_NOTES.md'),
    (Join-Path $stageRoot 'docs\TEMPLATES.zh-CN.md'),
    (Join-Path $stageRoot 'docs\LEVEL_CATALOG.zh-CN.md'),
    (Join-Path $stageRoot 'docs\ENTITY_LAYOUT.md'),
    (Join-Path $stageRoot 'docs\SOURCE_REFERENCES.md'),
    (Join-Path $stageRoot 'docs\THIRD_PARTY_NOTICES.md'),
    (Join-Path $stageRoot 'licenses\Python-LICENSE.txt'),
    (Join-Path $stageRoot 'licenses\native\minhook\LICENSE.txt'),
    (Join-Path $stageRoot 'licenses\native\nlohmann\LICENSE.MIT'))) {
    if (-not (Test-Path -LiteralPath $expected)) { throw "Release output is incomplete: $expected" }
}

$inno = $InnoCompilerPath
if (-not $inno) {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
        (Join-Path $projectRoot '.tmp\packaging-tools\innosetup-7.1.0\portable\ISCC.exe'),
        (Join-Path $projectRoot '.tmp\packaging-tools\innosetup-7.0.2\portable\ISCC.exe')
    )
    $inno = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $SkipInstaller) {
    if (-not $inno) { throw 'ISCC.exe was not found. Pass -InnoCompilerPath or use -SkipInstaller.' }
    & $inno (Join-Path $projectRoot 'packaging\installer.iss')
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
}

& $python (Join-Path $projectRoot 'packaging\write_manifest.py') $stageRoot `
    (Join-Path $releaseRoot 'release-manifest.json') (Join-Path $releaseRoot 'package-tree.txt')
if ($LASTEXITCODE -ne 0) { throw "Release manifest generation failed with exit code $LASTEXITCODE." }
Write-Output "Package: $stageRoot"
Write-Output "Manifest: $(Join-Path $releaseRoot 'release-manifest.json')"
if (-not $SkipInstaller) { Write-Output "Installer: $(Join-Path $releaseRoot 'pvz-ai-setup-0.1.0.exe')" }
