#!/usr/bin/env bash
# Komorebi: Protocol Zero-Seven — macOS Universal (arm64 + x86_64, Metal SM5) Build & Packaging Script
# Targets: Universal macOS Client + Dedicated Server
#
# Environment variables (all optional):
#   UE_ROOT       Path to Unreal Engine 5.4/5.5 root (auto-detected if unset)
#   BLENDER       Path to Blender 4.2 LTS executable (auto-detected if unset)
#   CONFIG        UE build configuration: Development | Shipping | DebugGame (default: Development)
#   ARCH          Target macOS architecture(s) (default: arm64+x86_64)
#   ARCHIVE_DIR   Output staging/archive directory (default: <repo>/Build/Mac)
#   SKIP_TESTS    Set to 1 to skip CMake/CTest KomorebiCoreTests
#   SKIP_ASSETS   Set to 1 to skip Blender validation/FBX export
#   SKIP_IMPORT   Set to 1 to skip UnrealEditor-Cmd Python import & map build
#   SKIP_PACKAGE  Set to 1 to compile binaries only without RunUAT BuildCookRun

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_FILE="${PROJECT_ROOT}/Komorebi.uproject"
CONFIG="${CONFIG:-Development}"
ARCH="${ARCH:-arm64+x86_64}"
ARCHIVE_DIR="${ARCHIVE_DIR:-${PROJECT_ROOT}/Build/Mac}"
SKIP_TESTS="${SKIP_TESTS:-0}"
SKIP_ASSETS="${SKIP_ASSETS:-0}"
SKIP_IMPORT="${SKIP_IMPORT:-0}"
SKIP_PACKAGE="${SKIP_PACKAGE:-0}"

log() {
  printf '[Komorebi macOS] %s\n' "$*"
}

die() {
  printf '[Komorebi macOS][ERROR] %s\n' "$*" >&2
  exit 1
}

resolve_ue_root() {
  if [[ -n "${UE_ROOT:-}" && -d "${UE_ROOT}" ]]; then
    printf '%s' "${UE_ROOT}"
    return 0
  fi
  local candidate
  for candidate in \
    "/Users/Shared/Epic Games/UE_5.5" \
    "/Users/Shared/Epic Games/UE_5.4" \
    "${HOME}/UnrealEngine" \
    "/Applications/UnrealEngine"; do
    if [[ -x "${candidate}/Engine/Build/BatchFiles/Mac/Build.sh" ]]; then
      printf '%s' "${candidate}"
      return 0
    fi
  done
  return 1
}

resolve_blender() {
  if [[ -n "${BLENDER:-}" && -x "${BLENDER}" ]]; then
    printf '%s' "${BLENDER}"
    return 0
  fi
  local candidate
  for candidate in \
    "/Applications/Blender.app/Contents/MacOS/Blender" \
    "${HOME}/Applications/Blender.app/Contents/MacOS/Blender" \
    "${PROJECT_ROOT}/.test/blender/blender"; do
    if [[ -x "${candidate}" ]]; then
      printf '%s' "${candidate}"
      return 0
    fi
  done
  if command -v blender >/dev/null 2>&1; then
    command -v blender
    return 0
  fi
  return 1
}

# -----------------------------------------------------------------------------
# Step 1: Build & run pure C++20 KomorebiCoreTests via CMake/CTest
# -----------------------------------------------------------------------------
if [[ "${SKIP_TESTS}" != "1" ]]; then
  CMAKE_SRC=""
  if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
    CMAKE_SRC="${PROJECT_ROOT}"
  elif [[ -f "${PROJECT_ROOT}/Tests/CMakeLists.txt" ]]; then
    CMAKE_SRC="${PROJECT_ROOT}/Tests"
  fi

  if [[ -n "${CMAKE_SRC}" ]]; then
    command -v cmake >/dev/null 2>&1 || die "cmake not found on PATH (brew install cmake)"
    log "Step 1/5: Building & running KomorebiCoreTests (CMake/CTest)..."
    CORE_BUILD_DIR="${PROJECT_ROOT}/Build/CoreTests"
    cmake -S "${CMAKE_SRC}" -B "${CORE_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${CORE_BUILD_DIR}" --parallel
    ctest --test-dir "${CORE_BUILD_DIR}" --output-on-failure
  else
    log "Step 1/5: No CMakeLists.txt found yet; skipping standalone KomorebiCoreTests."
  fi
else
  log "Step 1/5: Skipped KomorebiCoreTests (SKIP_TESTS=1)."
fi

# -----------------------------------------------------------------------------
# Step 2: Validate & export UE5-calibrated FBX + portrait PNGs via Blender 4.2
# -----------------------------------------------------------------------------
if [[ "${SKIP_ASSETS}" != "1" ]]; then
  if BLENDER_BIN="$(resolve_blender)"; then
    log "Step 2/5: Validating & exporting RawAssets via ${BLENDER_BIN}..."
    "${BLENDER_BIN}" --background --factory-startup --disable-autoexec \
      --python-exit-code 1 \
      --python "${PROJECT_ROOT}/Scripts/export_fbx_from_blender.py" -- --export
  else
    log "Step 2/5: Blender not found (set BLENDER=... or install Blender.app); skipping FBX export."
  fi
else
  log "Step 2/5: Skipped Blender asset pipeline (SKIP_ASSETS=1)."
fi

# -----------------------------------------------------------------------------
# Step 3: Build KomorebiEditor, Komorebi (Universal), and KomorebiServer
# -----------------------------------------------------------------------------
UE_ROOT="$(resolve_ue_root)" || die "UE_ROOT not found. Export UE_ROOT=\"/Users/Shared/Epic Games/UE_5.5\" and re-run."
BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Mac/Build.sh"
UAT_SH="${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh"
EDITOR_CMD="${UE_ROOT}/Engine/Binaries/Mac/UnrealEditor-Cmd"

[[ -x "${BUILD_SH}" ]] || die "Missing UE5 Mac Build.sh at ${BUILD_SH}"
[[ -x "${UAT_SH}" ]] || die "Missing UE5 RunUAT.sh at ${UAT_SH}"

log "Step 3/5: Compiling UE5 macOS Metal SM5 targets (UE_ROOT=${UE_ROOT}, CONFIG=${CONFIG}, ARCH=${ARCH})..."
"${BUILD_SH}" KomorebiEditor Mac Development -Project="${PROJECT_FILE}" -WaitMutex -NoHotReloadFromIDE
"${BUILD_SH}" Komorebi Mac "${CONFIG}" -Project="${PROJECT_FILE}" -architecture="${ARCH}" -WaitMutex -NoHotReloadFromIDE
"${BUILD_SH}" KomorebiServer Mac "${CONFIG}" -Project="${PROJECT_FILE}" -architecture="${ARCH}" -WaitMutex -NoHotReloadFromIDE

# -----------------------------------------------------------------------------
# Step 4: Headless asset import, MPC/Material compilation, Input & Map build
# -----------------------------------------------------------------------------
if [[ "${SKIP_IMPORT}" != "1" ]]; then
  [[ -x "${EDITOR_CMD}" ]] || die "Missing UnrealEditor-Cmd at ${EDITOR_CMD}"
  log "Step 4/5: Running UnrealEditor-Cmd Python asset import & level builder..."
  "${EDITOR_CMD}" "${PROJECT_FILE}" \
    -ExecutePythonScript="${PROJECT_ROOT}/Scripts/ue_import_and_build.py" \
    -NullRHI -NoSound -Unattended -NoSplash -NoP4 -stdout -FullStdOutLogOutput
else
  log "Step 4/5: Skipped UnrealEditor-Cmd Python import (SKIP_IMPORT=1)."
fi

# -----------------------------------------------------------------------------
# Step 5: Package Universal macOS Metal SM5 Client + Dedicated Server
# -----------------------------------------------------------------------------
if [[ "${SKIP_PACKAGE}" != "1" ]]; then
  mkdir -p "${ARCHIVE_DIR}/Client" "${ARCHIVE_DIR}/Server"

  log "Step 5/5 (Client): Packaging Universal macOS (${ARCH}, Metal SM5) Client -> ${ARCHIVE_DIR}/Client..."
  "${UAT_SH}" BuildCookRun \
    -project="${PROJECT_FILE}" \
    -noP4 \
    -platform=Mac \
    -targetplatform=Mac \
    -specifiedarchitecture="${ARCH}" \
    -clientconfig="${CONFIG}" \
    -cook -build -stage -pak -archive \
    -archivedirectory="${ARCHIVE_DIR}/Client" \
    -map=L_NeoKyoto+L_Snowbound \
    -utf8output

  log "Step 5/5 (Server): Packaging Universal macOS (${ARCH}) Dedicated Server -> ${ARCHIVE_DIR}/Server..."
  "${UAT_SH}" BuildCookRun \
    -project="${PROJECT_FILE}" \
    -noP4 \
    -platform=Mac \
    -serverplatform=Mac \
    -specifiedarchitecture="${ARCH}" \
    -server -noclient \
    -serverconfig="${CONFIG}" \
    -cook -build -stage -pak -archive \
    -archivedirectory="${ARCHIVE_DIR}/Server" \
    -map=L_NeoKyoto+L_Snowbound \
    -utf8output

  log "macOS Universal (${ARCH}) Client & Dedicated Server packaging complete!"
else
  log "Step 5/5: Skipped RunUAT packaging (SKIP_PACKAGE=1)."
fi
