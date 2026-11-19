cmake_minimum_required(VERSION 3.20)
project(KomorebiCoreTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KOMOREBI_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Source/KomorebiCore")

add_library(KomorebiCoreStatic STATIC
  "${KOMOREBI_CORE_ROOT}/Private/KomorebiCoreModule.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/World/KomorebiNoise.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/World/WorldGrid.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/World/NeoKyotoTerrain.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/World/SnowboundTerrain.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/ChaptersData.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/LevelsData.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/CombatRules.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/TacticsRules.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/DuelStateMachine.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/VehiclePhysics.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/DayNightCycle.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/StoryDatabase.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/OnlineMeta.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/ShowcaseData.cpp"
  "${KOMOREBI_CORE_ROOT}/Private/Game/AudioTheory.cpp"
)

target_include_directories(KomorebiCoreStatic PUBLIC
  "${KOMOREBI_CORE_ROOT}/Public"
)

if (MSVC)
  target_compile_options(KomorebiCoreStatic PRIVATE /W4 /WX)
else()
  target_compile_options(KomorebiCoreStatic PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(KomorebiCoreTests
  KomorebiCoreTests.cpp
)

target_link_libraries(KomorebiCoreTests PRIVATE KomorebiCoreStatic)

if (MSVC)
  target_compile_options(KomorebiCoreTests PRIVATE /W4 /WX)
else()
  target_compile_options(KomorebiCoreTests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME TestCoordBridge COMMAND KomorebiCoreTests coord)
add_test(NAME TestNoiseAndGrid COMMAND KomorebiCoreTests grid)
add_test(NAME TestTerrains COMMAND KomorebiCoreTests terrain)
add_test(NAME TestChaptersAndLevels COMMAND KomorebiCoreTests levels)
add_test(NAME TestConfigTacticsCombat COMMAND KomorebiCoreTests combat)
add_test(NAME TestDuels COMMAND KomorebiCoreTests duels)
add_test(NAME TestVehiclePhysics COMMAND KomorebiCoreTests vehicles)
add_test(NAME TestDayNightStoryOnlineAudio COMMAND KomorebiCoreTests story)
