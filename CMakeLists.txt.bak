cmake_minimum_required(VERSION 3.16)
project(FelixTheCatRecomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Shared Genesis recompilation framework (pinned v0.1.2 checkout in sdk/).
# Pass -DGENESIS_RECOMP_ROOT=<path> to build against another checkout.
set(GENESIS_RECOMP_ROOT "" CACHE PATH
    "Optional shared Genesis framework checkout (defaults to ./sdk)")
if(GENESIS_RECOMP_ROOT)
    set(RECOMP_ROOT "${GENESIS_RECOMP_ROOT}")
else()
    set(RECOMP_ROOT "${CMAKE_SOURCE_DIR}/sdk")
endif()
set(RUNNER_ROOT "${RECOMP_ROOT}/runner")

if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH
        "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5/cmake")
endif()
find_package(SDL2 REQUIRED)

set(SDL2_PARENT_INCLUDE_DIRS "")
foreach(_inc IN LISTS SDL2_INCLUDE_DIRS)
    get_filename_component(_inc_name "${_inc}" NAME)
    if(_inc_name STREQUAL "SDL2")
        get_filename_component(_inc_parent "${_inc}" DIRECTORY)
        list(APPEND SDL2_PARENT_INCLUDE_DIRS "${_inc_parent}")
    endif()
endforeach()

include("${RECOMP_ROOT}/cmake/GenesisRecompGenerated.cmake")

genesisrecomp_add_generated_sources(
    FELIX_GENERATED_SOURCES felix "${CMAKE_SOURCE_DIR}" "Felix the Cat (Unl).bin")

genesisrecomp_runner_sources(FELIX_RUNNER_SOURCES TRACE ON)

set(FELIX_BACKEND_SOURCES
    "${RUNNER_ROOT}/video/genesis_vdp.c"
    "${RUNNER_ROOT}/video/genesis_bus.c"
    "${RUNNER_ROOT}/video/genesis_machine.c"
    "${RUNNER_ROOT}/external/superzazu/z80.c")

add_executable(FelixTheCatRecomp
    ${FELIX_RUNNER_SOURCES}
    ${FELIX_GENERATED_SOURCES}
    ${FELIX_BACKEND_SOURCES}
    "${CMAKE_SOURCE_DIR}/felix_spec.c"
)
add_dependencies(FelixTheCatRecomp ${FELIX_GENERATED_SOURCES_TARGET})

target_compile_definitions(FelixTheCatRecomp PRIVATE
    INCLUDE_GENERATED_CODE=1
    ENABLE_RECOMPILED_CODE=1
    OWN_BACKEND=1
    DEFAULT_DEBUG_PORT=4382
    GENESIS_GAME_VERSION="${PROJECT_VERSION}"
)

target_include_directories(FelixTheCatRecomp PRIVATE
    "${RUNNER_ROOT}"
    "${RUNNER_ROOT}/include"
    "${RUNNER_ROOT}/external/ymfm/src"
    "${RUNNER_ROOT}/external/superzazu"
    "${RUNNER_ROOT}/external/clowncommon"
    "${RUNNER_ROOT}/video"
    "${RECOMP_ROOT}/recompiler/src"
    "${CMAKE_SOURCE_DIR}"
    "${FELIX_GENERATED_SOURCES_DIR}"
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_PARENT_INCLUDE_DIRS}
)

target_link_libraries(FelixTheCatRecomp PRIVATE SDL2::SDL2main SDL2::SDL2)
if(WIN32)
    target_link_libraries(FelixTheCatRecomp PRIVATE ws2_32 comdlg32)
endif()

genesisrecomp_runner_target(FelixTheCatRecomp)

if(MSVC)
    target_compile_options(FelixTheCatRecomp PRIVATE /W3 /WX- /utf-8 /wd4102 /wd4996)
endif()

if(WIN32)
    add_custom_command(TARGET FelixTheCatRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:FelixTheCatRecomp>"
        COMMENT "Copying SDL2.dll")
endif()
