#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KOS_BASE_DIR="${KOS_BASE_DIR:-${ROOT_DIR}/kos}"
KOS_REPO="${KOS_REPO:-https://github.com/KallistiOS/KallistiOS.git}"
KOS_PORTS_REPO="${KOS_PORTS_REPO:-https://github.com/KallistiOS/kos-ports.git}"
DC_CHAIN_DIR="${DC_CHAIN_DIR:-${ROOT_DIR}/kos/toolchain}"
DC_CHAIN_REPO="${DC_CHAIN_REPO:-https://github.com/KallistiOS/kos-toolchain.git}"

KOS_TOOLCHAIN_SRC="${KOS_TOOLCHAIN_SRC:-}"
KOS_BASE_SRC="${KOS_BASE_SRC:-}"
KOS_PORTS_SRC="${KOS_PORTS_SRC:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--install-deps] [--toolchain] [--kos] [--ports] [--all]

Installs and configures a local KOS toolchain + environment for Dreamcast builds.

Options:
  --install-deps  Install host dependencies (apt-based).
  --toolchain     Build the sh-elf toolchain (kos-toolchain).
  --kos           Clone/update KallistiOS and build it.
  --ports         Clone/update kos-ports and build libppp.
  --all           Run all steps (default if no args).

Environment overrides:
  KOS_BASE_DIR    Destination for KallistiOS (default: ${ROOT_DIR}/kos)
  KOS_REPO        KallistiOS git URL
  KOS_PORTS_REPO  kos-ports git URL
  DC_CHAIN_DIR    Destination for toolchain
  DC_CHAIN_REPO   kos-toolchain git URL
  KOS_TOOLCHAIN_SRC  Local path or tarball for kos-toolchain (avoids git)
  KOS_BASE_SRC       Local path or tarball for KallistiOS (avoids git)
  KOS_PORTS_SRC      Local path or tarball for kos-ports (avoids git)
EOF
}

install_deps() {
  if ! command -v apt-get >/dev/null 2>&1; then
    echo "apt-get not found. Install dependencies manually." >&2
    exit 1
  fi

  sudo apt-get update
  sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    flex \
    bison \
    texinfo \
    libmpc-dev \
    libgmp-dev \
    libmpfr-dev \
    libisl-dev \
    libcloog-isl-dev \
    zlib1g-dev \
    libelf-dev \
    wget \
    curl \
    python3 \
    genisoimage \
    cdi4dc
}

extract_or_copy() {
  local src="$1"
  local dest="$2"

  mkdir -p "${dest}"
  if [[ -d "${src}" ]]; then
    if command -v rsync >/dev/null 2>&1; then
      rsync -a --delete "${src}/" "${dest}/"
    else
      rm -rf "${dest:?}/"*
      cp -a "${src}/." "${dest}/"
    fi
    return 0
  fi

  if [[ -f "${src}" ]]; then
    case "${src}" in
      *.tar.gz|*.tgz) tar -xzf "${src}" -C "${dest}" --strip-components=1 ;;
      *.tar.xz) tar -xJf "${src}" -C "${dest}" --strip-components=1 ;;
      *.zip)
        if command -v unzip >/dev/null 2>&1; then
          unzip -q "${src}" -d "${dest}"
        else
          python3 -m zipfile -e "${src}" "${dest}"
        fi
        ;;
      *) echo "Unsupported archive format: ${src}" >&2; return 1 ;;
    esac
    return 0
  fi

  echo "Source path not found: ${src}" >&2
  return 1
}

git_clone_or_update() {
  local repo="$1"
  local dest="$2"

  if [[ ! -d "${dest}/.git" ]]; then
    git clone "${repo}" "${dest}"
  else
    git -C "${dest}" fetch --all
    git -C "${dest}" pull --rebase
  fi
}

build_toolchain() {
  mkdir -p "${DC_CHAIN_DIR}"
  if [[ -n "${KOS_TOOLCHAIN_SRC}" ]]; then
    extract_or_copy "${KOS_TOOLCHAIN_SRC}" "${DC_CHAIN_DIR}"
  else
    git_clone_or_update "${DC_CHAIN_REPO}" "${DC_CHAIN_DIR}"
  fi

  export TARGET=sh-elf
  export PREFIX="${DC_CHAIN_DIR}/install"

  "${DC_CHAIN_DIR}/download.sh"
  "${DC_CHAIN_DIR}/build.sh"
}

build_kos() {
  if [[ -n "${KOS_BASE_SRC}" ]]; then
    extract_or_copy "${KOS_BASE_SRC}" "${KOS_BASE_DIR}"
  else
    git_clone_or_update "${KOS_REPO}" "${KOS_BASE_DIR}"
  fi

  export KOS_BASE="${KOS_BASE_DIR}"
  export KOS_CC_PREFIX="${DC_CHAIN_DIR}/install/bin/sh-elf-"
  export KOS_BIN_DIR="${DC_CHAIN_DIR}/install/bin"

  make -C "${KOS_BASE_DIR}"
}

build_ports() {
  local ports_dir="${ROOT_DIR}/kos-ports"
  if [[ -n "${KOS_PORTS_SRC}" ]]; then
    extract_or_copy "${KOS_PORTS_SRC}" "${ports_dir}"
  else
    git_clone_or_update "${KOS_PORTS_REPO}" "${ports_dir}"
  fi

  export KOS_BASE="${KOS_BASE_DIR}"
  export KOS_CC_PREFIX="${DC_CHAIN_DIR}/install/bin/sh-elf-"
  export KOS_BIN_DIR="${DC_CHAIN_DIR}/install/bin"

  make -C "${ports_dir}" libppp
}

main() {
  if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
  fi

  local do_deps=false
  local do_toolchain=false
  local do_kos=false
  local do_ports=false

  if [[ $# -eq 0 || "${1:-}" == "--all" ]]; then
    do_deps=true
    do_toolchain=true
    do_kos=true
    do_ports=true
  else
    while [[ $# -gt 0 ]]; do
      case "$1" in
        --install-deps) do_deps=true ;;
        --toolchain) do_toolchain=true ;;
        --kos) do_kos=true ;;
        --ports) do_ports=true ;;
        --all) do_deps=true; do_toolchain=true; do_kos=true; do_ports=true ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
      esac
      shift
    done
  fi

  if $do_deps; then install_deps; fi
  if $do_toolchain; then build_toolchain; fi
  if $do_kos; then build_kos; fi
  if $do_ports; then build_ports; fi

  cat <<EOF
KOS environment ready.
Export these in your shell (or add to your profile):
  export KOS_BASE="${KOS_BASE_DIR}"
  export KOS_CC_PREFIX="${DC_CHAIN_DIR}/install/bin/sh-elf-"
  export KOS_BIN_DIR="${DC_CHAIN_DIR}/install/bin"

Then build the Discross CDI:
  ${ROOT_DIR}/tools/build_discross_cdi.sh
EOF
}

main "$@"
