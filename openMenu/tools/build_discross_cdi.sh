#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build-discross}"
STAGE_DIR="${BUILD_DIR}/discross_cdi"
IP_TEMPLATE="${ROOT_DIR}/tools/discross_ip.txt"

if [[ -z "${KOS_BASE:-}" ]]; then
  echo "KOS_BASE is not set. Please export KOS_BASE before running this script." >&2
  exit 1
fi

MAKEIP="${MAKEIP:-}"
if [[ -z "${MAKEIP}" ]]; then
  if [[ -x "${KOS_BASE}/utils/makeip" ]]; then
    MAKEIP="${KOS_BASE}/utils/makeip"
  elif command -v makeip >/dev/null 2>&1; then
    MAKEIP="$(command -v makeip)"
  fi
fi

if [[ -z "${MAKEIP}" ]]; then
  echo "makeip not found. Please install KOS utils or set MAKEIP to the makeip binary." >&2
  exit 1
fi

MKISOFS_BIN=""
if command -v mkisofs >/dev/null 2>&1; then
  MKISOFS_BIN="$(command -v mkisofs)"
elif command -v genisoimage >/dev/null 2>&1; then
  MKISOFS_BIN="$(command -v genisoimage)"
fi

if [[ -z "${MKISOFS_BIN}" ]]; then
  echo "mkisofs/genisoimage not found. Please install genisoimage." >&2
  exit 1
fi

CDI4DC="${CDI4DC:-}"
if [[ -z "${CDI4DC}" ]]; then
  if [[ -x "${KOS_BASE}/utils/cdi4dc" ]]; then
    CDI4DC="${KOS_BASE}/utils/cdi4dc"
  elif command -v cdi4dc >/dev/null 2>&1; then
    CDI4DC="$(command -v cdi4dc)"
  fi
fi

if [[ -z "${CDI4DC}" ]]; then
  echo "cdi4dc not found. Please install cdi4dc or set CDI4DC to the binary path." >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DBUILD_DREAMCAST=ON -DBUILD_PC=OFF
cmake --build "${BUILD_DIR}" --target discross_bin

DISCROSS_BIN="${BUILD_DIR}/src/openmenu/discross/1ST_READ.BIN"
if [[ ! -f "${DISCROSS_BIN}" ]]; then
  echo "1ST_READ.BIN not found at ${DISCROSS_BIN}." >&2
  exit 1
fi

mkdir -p "${STAGE_DIR}"
cp -f "${DISCROSS_BIN}" "${STAGE_DIR}/1ST_READ.BIN"

if [[ ! -f "${IP_TEMPLATE}" ]]; then
  echo "Missing ${IP_TEMPLATE}. Please create an IP template before building CDI." >&2
  exit 1
fi

"${MAKEIP}" "${IP_TEMPLATE}" "${STAGE_DIR}/IP.BIN"

"${MKISOFS_BIN}" -C 0,11702 -V "DISCROSS" -G "${STAGE_DIR}/IP.BIN" -J -l -o "${STAGE_DIR}/discross.iso" "${STAGE_DIR}"
"${CDI4DC}" "${STAGE_DIR}/discross.iso" "${STAGE_DIR}/discross.cdi"

echo "CDI created at ${STAGE_DIR}/discross.cdi"
