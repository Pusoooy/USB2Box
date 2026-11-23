#!/usr/bin/env bash
set -euo pipefail

# Debian trixie x86-64 cloud runtime. Extract signed-archive packages without sudo.
tools_dir=${USB2BOX_TOOLS_DIR:-/workspace/usb2box-tools}
apt_dir="$tools_dir/apt"
arm_dir="$tools_dir/arm"
arm_bin="$arm_dir/usr/bin"
root=$(cd "$(dirname "$0")/.." && pwd)
offline=${USB2BOX_OFFLINE_CACHE:-}
if [[ -n "$offline" ]]; then offline=$(realpath "$offline"); fi
if [[ ! -x "$arm_bin/arm-none-eabi-gcc" || ! -x "$arm_bin/arm-none-eabi-objcopy" ||
      ! -f "$arm_dir/usr/include/newlib/stdio.h" ]]; then
    package_dir="$apt_dir"
    if [[ -n "$offline" ]]; then
        package_dir="$offline"
    else
        mkdir -p "$apt_dir/lists/partial" "$apt_dir/cache/archives/partial"
        printf '%s\n' 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main' > "$apt_dir/sources.list"
        apt_args=(
            -o "Dir::Etc::sourcelist=$apt_dir/sources.list"
            -o Dir::Etc::sourceparts=-
            -o "Dir::State::lists=$apt_dir/lists"
            -o "Dir::Cache=$apt_dir/cache"
        )
        apt-get "${apt_args[@]}" update
        (
            cd "$apt_dir"
            apt-get "${apt_args[@]}" download 'gcc-arm-none-eabi=15:14.2.rel1-1' \
                'binutils-arm-none-eabi=2.44-3+23+b1' 'libnewlib-dev=4.5.0.20241231-1'
        )
    fi
    (
        cd "$package_dir"
        sha256sum --check "$root/tools/debian-arm-amd64.sha256"
        dpkg-deb -x 'gcc-arm-none-eabi_15%3a14.2.rel1-1_amd64.deb' "$arm_dir"
        dpkg-deb -x 'binutils-arm-none-eabi_2.44-3+23+b1_amd64.deb' "$arm_dir"
        dpkg-deb -x 'libnewlib-dev_4.5.0.20241231-1_all.deb' "$arm_dir"
    )
fi

export PATH="$arm_bin:$PATH"
dependency_dir="$root/.deps"
if [[ -n "$offline" ]]; then
    dependency_dir="$tools_dir/arm-deps"
    mkdir -p "$dependency_dir"
    while read -r component checksum _; do
        [[ -z "$component" || "$component" == \#* ]] && continue
        filename="$component.tar.gz"
        (cd "$offline" && printf '%s  %s\n' "$checksum" "$filename" | sha256sum --check)
        cp "$offline/$filename" "$dependency_dir/$filename"
    done < "$root/tools/arm-dependencies.lock"
fi
make -C "$root" ARM_DEPS="$dependency_dir" fetch-deps
make -C "$root" ARM_DEPS="$dependency_dir" offline-verify
