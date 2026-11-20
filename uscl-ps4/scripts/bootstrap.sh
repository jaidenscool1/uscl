#!/usr/bin/env bash
set -euo pipefail
task_ps4dev="${PS4DEV:-$HOME/ps4dev}"
mkdir -p "$task_ps4dev/downloads"
task_archive="$task_ps4dev/downloads/toolchain-llvm-18.tar.gz"
if [[ ! -f "$task_archive" ]]; then
    curl -fL --retry 3 https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz -o "$task_archive"
fi
echo "3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526  $task_archive" | sha256sum -c -
if [[ ! -d "$task_ps4dev/OpenOrbis/PS4Toolchain" ]]; then
    tar -xzf "$task_archive" -C "$task_ps4dev"
fi
checkout() {
    local name="$1" url="$2" commit="$3"
    if [[ ! -d "$task_ps4dev/$name" ]]; then git clone "$url" "$task_ps4dev/$name"; fi
    if [[ "$(git -C "$task_ps4dev/$name" rev-parse HEAD)" != "$commit" ]]; then
        if [[ -n "$(git -C "$task_ps4dev/$name" status --porcelain)" ]]; then
            echo "Preserving modified dependency: $task_ps4dev/$name" >&2; exit 1
        fi
        git -C "$task_ps4dev/$name" checkout --detach "$commit"
    fi
}
checkout libjbc https://github.com/bucanero/ps4-libjbc.git 835fe016ff0ae5dd89b9249f39cc0fe093fd07dd
checkout LibOrbisPkg https://github.com/OpenOrbis/LibOrbisPkg.git 92a4e7f8e5914cb06b52ba3ae6b0675da438a051
task_dotnet="${DOTNET:-dotnet}"
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 "$task_dotnet" publish \
    "$task_ps4dev/LibOrbisPkg/PkgTool.Core/PkgTool.Core.csproj" -c Release -r linux-x64 \
    --self-contained true -p:PublishSingleFile=true -o "$task_ps4dev/pkgtool8"
echo "Build with: make PKGTOOL=$task_ps4dev/pkgtool8/PkgTool.Core package validate test"
