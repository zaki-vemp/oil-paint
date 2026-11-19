#!/usr/bin/env bash
# Komorebi: Protocol Zero-Seven — Linux x86_64 Build, Asset Import & Packaging Script
# Targets: Vulkan SM5 Client + Linux Dedicated Server
#
# Environment variables (all optional):
#   UE_ROOT       Path to Unreal Engine 5.4/5.5 root (auto-detected if unset)
#   BLENDER       Path to Blender 4.2 LTS executable (defaults to .test/blender/blender or PATH)
#   CONFIG        UE build configuration: Development | Shipping | DebugGame (default: Development)
#   ARCHIVE_DIR   Output staging/archive directory (default: <repo>/Build/Linux)
#   SKIP_TESTS    Set to 1 to skip CMake/CTest KomorebiCoreTests
#   SKIP_ASSETS   Set to 1 to skip Blender validation/FBX export
#   SKIP_IMPORT   Set to 1 to skip UnrealEditor-Cmd Python import & map build
#   SKIP_PACKAGE  Set to 1 to compile binaries only without RunUAT BuildCookRun

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_FILE="${PROJECT_ROOT}/Komorebi.uproject"
CONFIG="${CONFIG:-Development}"
ARCHIVE_DIR="${ARCHIVE_DIR:-${PROJECT_ROOT}/Build/Linux}"
SKIP_TESTS="${SKIP_TESTS:-0}"
SKIP_ASSETS="${SKIP_ASSETS:-0}"
SKIP_IMPORT="${SKIP_IMPORT:-0}"
SKIP_PACKAGE="${SKIP_PACKAGE:-0}"

log() {
  printf '[Komorebi Linux] %s\n' "$*"
}

die() {
  printf '[Komorebi Linux][ERROR] %s\n' "$*" >&2
  exit 1
}

resolve_ue_root() {
  if [[ -n "${UE_ROOT:-}" && -d "${UE_ROOT}" ]]; then
    printf '%s' "${UE_ROOT}"
    return 0
  fi
  local candidate
  for candidate in \
    "${HOME}/UnrealEngine" \
    "/opt/UnrealEngine" \
    "/opt/UnrealEngine-5.5" \
    "/usr/local/UnrealEngine" \
    "${HOME}/UE_5.5" \
    "${HOME}/UE_5.4"; do
    if [[ -x "${candidate}/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
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
  if [[ -x "${PROJECT_ROOT}/.test/blender/blender" ]]; then
    printf '%s' "${PROJECT_ROOT}/.test/blender/blender"
    return 0
  fi
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
    command -v cmake >/dev/null 2>&1 || die "cmake not found on PATH (required for KomorebiCoreTests)"
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
    log "Step 2/5: Blender not found (set BLENDER=... or install in .test/blender); skipping FBX export."
  fi
else
  log "Step 2/5: Skipped Blender asset pipeline (SKIP_ASSETS=1)."
fi

# -----------------------------------------------------------------------------
# Step 3: Build KomorebiEditor, Komorebi (Client), and KomorebiServer via UBT
# -----------------------------------------------------------------------------
UE_ROOT="$(resolve_ue_root)" || die "UE_ROOT not found. Export UE_ROOT=/path/to/UnrealEngine_5.5 and re-run."
BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Linux/Build.sh"
UAT_SH="${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh"
EDITOR_CMD="${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd"

[[ -x "${BUILD_SH}" ]] || die "Missing UE5 Linux Build.sh at ${BUILD_SH}"
[[ -x "${UAT_SH}" ]] || die "Missing UE5 RunUAT.sh at ${UAT_SH}"

log "Step 3/5: Compiling UE5 targets (UE_ROOT=${UE_ROOT}, CONFIG=${CONFIG})..."
"${BUILD_SH}" KomorebiEditor Linux Development -Project="${PROJECT_FILE}" -WaitMutex -NoHotReloadFromIDE
"${BUILD_SH}" Komorebi Linux "${CONFIG}" -Project="${PROJECT_FILE}" -WaitMutex -NoHotReloadFromIDE
"${BUILD_SH}" KomorebiServer Linux "${CONFIG}" -Project="${PROJECT_FILE}" -WaitMutex -NoHotReloadFromIDE

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
# Step 5: Package Linux x86_64 Client + Dedicated Server via RunUAT BuildCookRun
# -----------------------------------------------------------------------------
if [[ "${SKIP_PACKAGE}" != "1" ]]; then
  mkdir -p "${ARCHIVE_DIR}/Client" "${ARCHIVE_DIR}/Server"

  log "Step 5/5 (Client): Packaging Linux x86_64 Vulkan SM5 Client -> ${ARCHIVE_DIR}/Client..."
  "${UAT_SH}" BuildCookRun \
    -project="${PROJECT_FILE}" \
    -noP4 \
    -platform=Linux \
    -targetplatform=Linux \
    -clientconfig="${CONFIG}" \
    -cook -build -stage -pak -archive \
    -archivedirectory="${ARCHIVE_DIR}/Client" \
    -map=L_NeoKyoto+L_Snowbound \
    -utf8output

  log "Step 5/5 (Server): Packaging Linux x86_64 Dedicated Server -> ${ARCHIVE_DIR}/Server..."
  "${UAT_SH}" BuildCookRun \
    -project="${PROJECT_FILE}" \
    -noP4 \
    -platform=Linux \
    -serverplatform=Linux \
    -server -noclient \
    -serverconfig="${CONFIG}" \
    -cook -build -stage -pak -archive \
    -archivedirectory="${ARCHIVE_DIR}/Server" \
    -map=L_NeoKyoto+L_Snowbound \
    -utf8output

  log "Linux x86_64 Client & Dedicated Server packaging complete!"
else
  log "Step 5/5: Skipped RunUAT packaging (SKIP_PACKAGE=1)."
fi
