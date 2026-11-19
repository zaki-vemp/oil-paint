# AGENTS.md — Komorebi (Unreal Engine 5 C++)

Instructions and navigation for coding agents working in this repository.
The game has been ported from JavaScript/Three.js (temporarily archived in `legacy-web/`) to native cross-platform **Unreal Engine 5.4 / 5.5 LTS C++20** targeting **Linux (Vulkan SM5)**, **macOS (Metal SM5 Universal)**, and **Windows (DX12/Vulkan)** with zero JS/Node runtime dependencies.

## Architecture (3 Layers)

1. **`Source/KomorebiCore/`** — Engine-agnostic pure C++20 simulation, deterministic noise/terrains, coordinate bridge, chapters, levels, combat/tactics/duel state machines, arcade vehicle physics, day/night cycle, story/lore/missions database, online unlock tables, showcase curves, and audio theory.
   - Compiles with ZERO Unreal Engine headers (`KomorebiCoreAPI.h`), verified locally via `Tests/CMakeLists.txt` + `CTest` (`-Wall -Wextra -Werror`) AND linked as a UE5 Runtime Module (`KomorebiCore.Build.cs`).
2. **`Source/KomorebiGame/`** — Full Unreal Engine 5.4/5.5 C++20 runtime module:
   - `World/`: `AKomorebiTerrainActor`, `ANeoKyotoCityActor`, `AStreetPropsActor`, `ASnowboundWorldActor`, `ADayNightAtmosphereActor`, `UGraphicsSettingsSubsystem`.
   - `Character/`: `UKaelMovementComponent`, `UFollowCameraComponent`, `UKaelAnimInstance`, `AKaelCharacter`.
   - `Combat/`: `UBladeTrailComponent`, `UCombatComponent`.
   - `AI/`: `AStreetEnemyCharacter`, `UEnemySquadSubsystem`, `ARenBossCharacter`.
   - `Vehicles/`: `AKomorebiVehiclePawn`, `UTrafficSubsystem`.
   - `Campaign/`: `UKomorebiSaveGame`, `UMissionSubsystem`, `AShowcaseCameraDirector`, `UKomorebiWorldCoordinator`.
   - `Audio/`: `UKomorebiSynthComponent` (real-time AudioMixer C++ DSP), `UKomorebiAudioSubsystem`.
   - `UI/`: `AKomorebiHUD`, `SKomorebiHUD`, `SKomorebiPauseMenu` (3-tab Lore Codex), `SKomorebiShowcaseOverlay` (pure Slate).
   - `Network/`: `UKomorebiGameInstance`, `AKomorebiGameMode`, `AKomorebiOnlineGameMode`, `AKomorebiGameState`, `AKomorebiPlayerState`, `AKomorebiPlayerController`, `AOnlineActivitiesActor`, `UOnlineSessionSubsystem`.
3. **`Shaders/` & `Scripts/`** — SM5 HLSL library (`Shaders/KomorebiMaterialFunctions.ush`), Blender 4.2 FBX/PNG validator & exporter (`Scripts/export_fbx_from_blender.py`), headless UE Editor Python importer/material/map builder (`Scripts/ue_import_and_build.py`), and cross-platform packaging scripts (`Scripts/build_linux.sh`, `Scripts/build_macos.sh`, `Scripts/build_windows.bat`).

## Verify & Build Commands

```bash
# 1. Pure C++20 core simulation & invariant tests (runs anywhere with cmake + g++/clang++)
cmake -S Tests -B Tests/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Tests/build
ctest --test-dir Tests/build --output-on-failure -V

# 2. Validate all 26 street GLBs, protagonist/clip GLBs, and portrait SVGs via Blender 4.2 LTS
BLENDER=${BLENDER:-.test/blender/blender}
"$BLENDER" --background --factory-startup --disable-autoexec --python-exit-code 1 \
  --python Scripts/export_fbx_from_blender.py -- --check

# 3. Syntax-check Python & shell automation scripts
python3 -m py_compile Scripts/export_fbx_from_blender.py Scripts/ue_import_and_build.py
bash -n Scripts/build_linux.sh Scripts/build_macos.sh

# 4. Full Unreal Engine 5.4/5.5 compile, asset import, map build, cook & package
UE_ROOT=/path/to/UE_5.5 ./Scripts/build_linux.sh          # Linux Vulkan SM5 (Client + Server)
UE_ROOT="/Users/Shared/Epic Games/UE_5.5" ./Scripts/build_macos.sh  # macOS Metal SM5 Universal
```

## Non-Negotiable Rules

1. **Coordinate Bridge (`CoordBridge.h`)**: All pure data in `KomorebiCore` uses legacy Three.js metres `(x, y, z, yaw)`. Convert via `KomorebiCore::ThreeToUE()` (`UE.X = -Three.Z * 100`, `UE.Y = +Three.X * 100`, `UE.Z = +Three.Y * 100`) and `ThreeYawToUEYawDeg()` (`WrapAngle(PI - yaw) * 180 / PI`). Never duplicate hand-rolled conversions.
2. **SM5 Broad-Hardware Profile**: Keep `r.DynamicGlobalIlluminationMethod=0` and `r.Nanite.ProjectEnabled=False` in `Config/DefaultEngine.ini` so both Linux Vulkan SM5 and macOS Metal SM5 run reliably without requiring Lumen/Nanite hardware.
3. **Pure C++20 Core Isolation**: Never `#include` Unreal headers inside `Source/KomorebiCore/` except behind `#if defined(__UNREAL__)` in `KomorebiCoreAPI.h` and `KomorebiCoreModule.h/.cpp`. Always extend `Tests/KomorebiCoreTests.cpp` when tuning gameplay balance, terrain, duel, or vehicle math.
4. **UHT & Cross-Platform C++ Hygiene**: Every UObject/USTRUCT/UENUM header must have `.generated.h` as its final `#include`. Never use Windows-only headers (`Windows.h`) or JS/Node dependencies.
5. **Raw Assets**: Do not hand-edit binary `.glb`/`.fbx` files in `RawAssets/`. Edit the Blender 4.2 sculptors in `Scripts/blender/` and re-verify with `Scripts/export_fbx_from_blender.py --check`.
6. **Honest Verification Reporting**: If Unreal Engine 5 is not installed in the current environment, report clearly that `KomorebiCoreTests` (CMake/CTest), Blender asset validation, Python/shell syntax checks, and static UHT/C++ audits ran, while full UE5 UBT/UAT compilation and packaging require a UE 5.4/5.5 host on Linux/macOS.
