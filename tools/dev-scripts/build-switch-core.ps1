# Build the pinned, patched Eden frontend on Windows (MSVC). Windows twin of build-switch-core.sh.
# Needs Visual Studio 2022 with "Desktop development with C++" (it supplies CMake and Ninja)
# and the Vulkan SDK (for glslangValidator).
# Eden's MSVC configuration downloads Qt, SDL2, FFmpeg and OpenSSL itself.
$ErrorActionPreference = 'Continue'  # PS 5.1 makes native stderr fatal under Stop; exit codes are checked instead
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$source = if ($env:USCL_EDEN_SOURCE) { $env:USCL_EDEN_SOURCE } else { "$repo\build\eden-source" }
$prefix = "$repo\build\switch-core"
$base = '58c1e20ee58efa3900ba616207d460886214480b'
$jobs = if ($env:USCL_BUILD_JOBS) { $env:USCL_BUILD_JOBS } else { [Environment]::ProcessorCount }

function EdenGit { & git.exe -C $source @args; if ($LASTEXITCODE) { throw "git $args failed" } }

if (-not (Test-Path "$source\.git")) {
    # autocrlf=false: the USCL patches are LF and must apply byte for byte.
    & git -c core.autocrlf=false clone --depth 1 --branch v0.2.1 https://git.eden-emu.dev/eden-emu/eden.git $source
    if ($LASTEXITCODE) { throw 'clone failed' }
    EdenGit config core.autocrlf false
}
EdenGit merge-base --is-ancestor $base HEAD
if (EdenGit status --porcelain --untracked-files=no) { throw 'Eden checkout has uncommitted changes; commit or use a separate USCL_EDEN_SOURCE.' }
$patches = @(Get-ChildItem "$repo\uscl-eden\patches\*.patch" | Sort-Object Name)
# Reverse-apply checks misfire once later patches touch the same lines; count commits past the base instead.
$applied = [int](EdenGit rev-list --count "$base..HEAD")
if ($applied -gt $patches.Count) { throw 'Eden checkout has commits beyond the USCL patches; use a fresh USCL_EDEN_SOURCE.' }
foreach ($patch in $patches | Select-Object -Skip $applied) {
    EdenGit -c user.name=uscl -c user.email=uscl@localhost am $patch.FullName
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[17.0,18.0)' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 with the C++ workload is required.' }
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# Eden compiles its host shaders with glslangValidator; the LunarG Vulkan SDK installer supplies it.
$glslang = (Get-Command glslangValidator -ErrorAction SilentlyContinue).Source
if (-not $glslang -and $env:VULKAN_SDK) { $glslang = "$env:VULKAN_SDK\Bin\glslangValidator.exe" }
# Khronos release zips name the same tool glslang.exe; unzip one into build\tools\glslang.
if (-not $glslang -or -not (Test-Path $glslang)) { $glslang = "$repo\build\tools\glslang\bin\glslang.exe" }
if (-not (Test-Path $glslang)) { throw 'glslangValidator not found: install the Vulkan SDK, or unzip a Khronos glslang Windows release into build\tools\glslang.' }

$build = "$source\build-uscl"
cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DGLSLANGVALIDATOR=$glslang" `
    -DENABLE_QT=ON -DYUZU_TESTS=OFF -DBUILD_TESTING=OFF -DENABLE_WEB_SERVICE=OFF `
    -DYUZU_ROOM_STANDALONE=OFF -DYUZU_DISABLE_LLVM=ON
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $build --target yuzu -j $jobs
if ($LASTEXITCODE) { throw 'build failed' }

# The bin folder holds eden.exe plus the Qt/SDL/FFmpeg DLLs Eden's build copies beside it.
New-Item -ItemType Directory -Force $prefix | Out-Null
robocopy "$build\bin" "$prefix\bin" /MIR /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'copy failed' }
Write-Host "Built USCL Eden at $prefix\bin\eden.exe"
