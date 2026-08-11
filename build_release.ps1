param(
  [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingPath {
  param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$Candidates
  )

  foreach ($candidate in $Candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }

    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  return $null
}

function Get-VisualStudioRoots {
  $roots = @()

  $vswhere = Resolve-ExistingPath @(
    'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe',
    'C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe'
  )
  if ($null -ne $vswhere) {
    try {
      $found = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
      if ($null -ne $found) {
        $roots += @($found)
      }
    } catch {
    }
  }

  foreach ($drive in @('C:', 'D:', 'E:')) {
    foreach ($edition in @('BuildTools', 'Community', 'Professional', 'Enterprise')) {
      $roots += "$drive\Program Files (x86)\Microsoft Visual Studio\2022\$edition"
      $roots += "$drive\Program Files\Microsoft Visual Studio\2022\$edition"
    }
  }

  return $roots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
}

function Get-VersionMacroValue {
  param(
    [Parameter(Mandatory = $true)]
    [string]$HeaderPath,
    [Parameter(Mandatory = $true)]
    [string]$MacroName
  )

  $match = Select-String -Path $HeaderPath -Pattern ('^\s*#define\s+' + [Regex]::Escape($MacroName) + '\s+"([^"]+)"\s*$') | Select-Object -First 1
  if ($null -eq $match) {
    throw "无法从版本头文件中读取宏 $MacroName。"
  }

  return $match.Matches[0].Groups[1].Value
}

function Fail-WithMessage {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Message
  )

  Write-Error $Message
  exit 1
}

$repoRoot = Split-Path -Parent $PSCommandPath
$builderDir = Join-Path $repoRoot 'builder'
$userDocsSourceDir = Join-Path $repoRoot 'docs\user'
$windowsDir = Join-Path $repoRoot 'windows'
$versionHeader = Join-Path $builderDir 'src\core\version.h'
$readmeTemplatePath = Join-Path $builderDir 'release\README.template.txt'

if (!(Test-Path -LiteralPath $builderDir)) {
  Fail-WithMessage '未找到 builder 目录，请在项目根目录运行此脚本。'
}
if (!(Test-Path -LiteralPath $versionHeader)) {
  Fail-WithMessage '未找到版本头文件 builder\src\core\version.h。'
}
if (!(Test-Path -LiteralPath $readmeTemplatePath)) {
  Fail-WithMessage '未找到发布说明模板 builder\release\README.template.txt。'
}
if (!(Test-Path -LiteralPath $userDocsSourceDir)) {
  Fail-WithMessage '未找到用户文档目录 docs\user。'
}
$vsRoots = @(Get-VisualStudioRoots)
if ($vsRoots.Count -eq 0) {
  Fail-WithMessage '未找到 Visual Studio 2022（含 C++ 构建组件），请先安装 VS Build Tools 或 Community。'
}

$vsDevCmd = Resolve-ExistingPath @($vsRoots | ForEach-Object { Join-Path $_ 'Common7\Tools\VsDevCmd.bat' })
if ($null -eq $vsDevCmd) {
  Fail-WithMessage '未找到 VsDevCmd.bat，请确认 Visual Studio 安装完整。'
}

$cmake = Resolve-ExistingPath (@(
  'C:\Program Files\CMake\bin\cmake.exe'
) + ($vsRoots | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }))
if ($null -eq $cmake) {
  Fail-WithMessage '未找到 cmake.exe，请先安装 CMake 或 Visual Studio 自带的 CMake 组件。'
}

$ninja = Resolve-ExistingPath (@(
  'C:\Program Files\Ninja\ninja.exe'
) + ($vsRoots | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' }))
if ($null -eq $ninja) {
  Fail-WithMessage '未找到 ninja.exe，请先安装 Ninja 或 Visual Studio 自带的 Ninja 组件。'
}

$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (!(Test-Path -LiteralPath $ctest)) {
  Fail-WithMessage '未找到 ctest.exe，请确认 CMake 安装完整。'
}

$versionText = Get-VersionMacroValue -HeaderPath $versionHeader -MacroName 'IMG2BIN_VERSION_TEXT'
$versionSemver = Get-VersionMacroValue -HeaderPath $versionHeader -MacroName 'IMG2BIN_VERSION_SEMVER'

$toolNames = @('raw', 'imprle', 'rle', 'qoi', 'qoif', 'indexqoi')

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$tempBuildRoot = Join-Path $env:LOCALAPPDATA 'Temp\img2bin_tools_build_release'
$buildDir = Join-Path $tempBuildRoot $timestamp
$distRoot = Join-Path $repoRoot 'dist'
$releaseDir = Join-Path $distRoot ("img2bin-tools-{0}-windows-x64" -f $versionText)
if (Test-Path -LiteralPath $releaseDir) {
  Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
$releaseWindowsDir = Join-Path $releaseDir 'windows'
$releaseToolsDir = Join-Path $releaseWindowsDir 'tools'
$releaseReadmePath = Join-Path $releaseDir 'README.txt'
$releaseDocsDir = Join-Path $releaseDir 'docs'
$releaseUserDocsDir = Join-Path $releaseDocsDir 'user'
$releaseDecoderDir = Join-Path $releaseDir 'decoder'
$decoderSourceDir = Join-Path $builderDir 'src\decoder'
$repoToolsDir = Join-Path $windowsDir 'tools'
$buildScriptPath = Join-Path $buildDir 'run_release_build.cmd'

if (!(Test-Path -LiteralPath (Join-Path $decoderSourceDir 'img2bin_decode.c'))) {
  Fail-WithMessage '未找到参考解码器 builder\src\decoder\img2bin_decode.c。'
}

New-Item -ItemType Directory -Force -Path $tempBuildRoot, $buildDir, $distRoot, $releaseDir, $releaseWindowsDir, $releaseToolsDir, $releaseDocsDir, $releaseDecoderDir, $repoToolsDir | Out-Null

if (Test-Path -LiteralPath $releaseUserDocsDir) {
  Remove-Item -LiteralPath $releaseUserDocsDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $releaseUserDocsDir | Out-Null

$ctestLine = "`"$ctest`" --test-dir `"$buildDir`" --output-on-failure"
if ($SkipTests) {
  $ctestLine = 'echo Tests skipped by -SkipTests.'
}

$buildScript = @"
@echo off
call "$vsDevCmd" -arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set CC=cl
"$cmake" -S "$builderDir" -B "$buildDir" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja"
if errorlevel 1 exit /b %errorlevel%
"$cmake" --build "$buildDir" --config Release
if errorlevel 1 exit /b %errorlevel%
$ctestLine
exit /b %errorlevel%
"@
Set-Content -Path $buildScriptPath -Value $buildScript -Encoding ascii

Write-Host "使用临时构建目录: $buildDir"
Write-Host '开始执行 Release 构建与测试...'
cmd.exe /c $buildScriptPath
if ($LASTEXITCODE -ne 0) {
  Fail-WithMessage 'Release 构建或测试失败，未生成发布目录。'
}

$builtExePaths = @{}
foreach ($tool in $toolNames) {
  $builtExePaths[$tool] = Join-Path $buildDir "bin\img2bin_$tool.exe"
  if (!(Test-Path -LiteralPath $builtExePaths[$tool])) {
    Fail-WithMessage "构建完成后未找到 img2bin_$tool.exe。"
  }
}

$readmeTemplate = Get-Content -Path $readmeTemplatePath -Raw -Encoding UTF8
$readmeText = $readmeTemplate.Replace('__VERSION_TEXT__', $versionText).Replace('__VERSION_SEMVER__', $versionSemver)

foreach ($tool in $toolNames) {
  Copy-Item -LiteralPath $builtExePaths[$tool] -Destination (Join-Path $repoToolsDir "img2bin_$tool.exe") -Force
  Copy-Item -LiteralPath $builtExePaths[$tool] -Destination (Join-Path $releaseToolsDir "img2bin_$tool.exe") -Force
}
Copy-Item -Path (Join-Path $userDocsSourceDir '*') -Destination $releaseUserDocsDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $decoderSourceDir 'img2bin_decode.c') -Destination (Join-Path $releaseDecoderDir 'img2bin_decode.c') -Force
Copy-Item -LiteralPath (Join-Path $decoderSourceDir 'img2bin_decode.h') -Destination (Join-Path $releaseDecoderDir 'img2bin_decode.h') -Force

Set-Content -Path $releaseReadmePath -Value $readmeText -Encoding utf8

Write-Host ''
Write-Host '发布完成。'
Write-Host "取模工具目录: $repoToolsDir"
Write-Host "发布目录: $releaseDir"
Write-Host "发布说明: $releaseReadmePath"
