cmake_minimum_required(VERSION 3.24)

# Port of the Jones3D engine (infernal-machine-decomp, upstream commit 319a6fc2) to arm64 macOS.
# See PORT-PLAN.md for the phases and docs/PORTING.md for every deviation from the decomp.
project(J3DPort VERSION 0.1.0 LANGUAGES C)  # OBJC is added with the first .m file (Phase 2)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)
endif()

# J3D_DEBUG (j3d.h) follows NDEBUG. The decomp's release build is the reference, so NDEBUG is defined in every
# configuration unless the engine's debug asserts are asked for explicitly.
option(J3D_WERROR_SHORTEN "Treat -Wshorten-64-to-32 as an error (enable once the Phase 1 audit is closed)" OFF)
option(J3D_DEBUG_ASSERTS "Compile the engine's debug-only J3D_ASSERT checks" OFF)

set(J3D_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(J3D_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)

# j3d.h is generated from j3d.h.in, as the decomp's configure step did.
set(PROJECT_NAME_FOR_HEADER ${PROJECT_NAME})
configure_file(${J3D_ROOT}/Libs/j3dcore/j3d.h.in ${J3D_GENERATED}/j3dcore/j3d.h @ONLY)

# ---------------------------------------------------------------------------------------------------------------------
# Common compile settings, shared by every engine library through the j3d_common interface target.
# ---------------------------------------------------------------------------------------------------------------------
add_library(j3d_common INTERFACE)
target_include_directories(j3d_common INTERFACE
    ${J3D_GENERATED}
    ${J3D_ROOT}/Libs
    ${J3D_ROOT}
    ${J3D_ROOT}/platform/apple/shim
)
target_compile_definitions(j3d_common INTERFACE
    NONMATCHING_MATH        # C bodies for stdMath's x87 helpers (PORT-PLAN.md 7.1)
    J3D_PORT=1
)
if(NOT J3D_DEBUG_ASSERTS)
    target_compile_definitions(j3d_common INTERFACE NDEBUG)
endif()
target_compile_options(j3d_common INTERFACE
    # Semantics the 1999 code relies on (PORT-PLAN.md 7.1): type-punned access, signed wrap-around, and no fused
    # multiply-add so float results stay as close to x87 single-precision stores as arm64 allows.
    -fno-strict-aliasing
    -fwrapv
    -ffp-contract=off
    -fno-common
    # Pointer truncation is a hard error: these are the 64-bit bugs that corrupt memory silently.
    -Werror=int-to-pointer-cast
    -Werror=pointer-to-int-cast
    # Every 64-to-32 narrowing is reported. They are triaged in the Phase 1 64-bit audit; see docs/PORTING.md.
    -Wshorten-64-to-32
    $<$<BOOL:${J3D_WERROR_SHORTEN}>:-Werror=shorten-64-to-32>
    # The decomp is written in the MSVC dialect; these fire on idioms that are correct there.
    -Wno-parentheses
    -Wno-dangling-else
    -Wno-switch
    -Wno-pointer-sign
    -Wno-missing-braces
    -Wno-unused-value
    -Wno-comment
    -Wno-microsoft-anon-tag
)

# ---------------------------------------------------------------------------------------------------------------------
# Engine libraries. Sources are globbed per library; files whose job is the Windows platform layer are removed here
# and implemented by platform/ instead (PORT-PLAN.md section 3, decision 4).
# ---------------------------------------------------------------------------------------------------------------------
function(j3d_library name)
    cmake_parse_arguments(ARG "" "" "DIRS;EXCLUDE;EXTRA;DEPS" ${ARGN})
    set(sources)
    foreach(dir IN LISTS ARG_DIRS)
        file(GLOB_RECURSE found CONFIGURE_DEPENDS ${J3D_ROOT}/${dir}/*.c)
        list(APPEND sources ${found})
    endforeach()
    foreach(ex IN LISTS ARG_EXCLUDE)
        list(REMOVE_ITEM sources ${J3D_ROOT}/${ex})
    endforeach()
    list(APPEND sources ${ARG_EXTRA})
    add_library(${name} STATIC ${sources})
    target_link_libraries(${name} PUBLIC j3d_common ${ARG_DEPS})
endfunction()

j3d_library(zlib DIRS Libs/zlib)
# Upstream zlib 1.0.9 predates prototypes-by-default on some paths and uses K&R definitions in places.
target_compile_options(zlib PRIVATE -Wno-deprecated-non-prototype -Wno-implicit-function-declaration -Wno-shorten-64-to-32)

j3d_library(std
    DIRS Libs/std/General Libs/std/Win95
    EXCLUDE
        Libs/std/Win95/std3D.c          # Direct3D 6           -> platform (Phase 2)
        Libs/std/Win95/stdDisplay.c     # DirectDraw           -> platform (Phase 2)
        Libs/std/Win95/stdControl.c     # DirectInput          -> platform (Phase 3)
        Libs/std/Win95/stdComm.c        # DirectPlay           -> stub (the game has no multiplayer)
        Libs/std/Win95/stdConsole.c     # Win32 console        -> platform
        Libs/std/Win95/stdWin95.c       # HWND/HINSTANCE store -> platform
        Libs/std/Win95/stdGuid.c        # DirectX GUIDs        -> not needed
)

j3d_library(rdroid
    DIRS Libs/rdroid
    EXCLUDE
        Libs/rdroid/Primitives/rdModel3K.c      # SSE "Katmai" mesh path, dropped (PORT-PLAN.md Phase 2)
        Libs/rdroid/Primitives/rdModel3K_cpu.c  # its CPU probe (SEH)
    DEPS std
)

j3d_library(sound
    DIRS Libs/sound
    EXCLUDE
        Libs/sound/Driver.c             # DirectSound          -> platform (Phase 4)
    DEPS std rdroid
)

j3d_library(smush
    DIRS Libs/smush
    EXCLUDE
        Libs/smush/insane/Timer/Timer.c     # Win32 thread         -> platform (Phase 5)
        Libs/smush/insane/Sound/Dsnd.c      # DirectSound          -> platform (Phase 5)
        Libs/smush/insane/File/Stdfile.c    # Win32 file I/O       -> platform (Phase 5)
        Libs/smush/insane/File/Stdio.c      #                      -> platform (Phase 5)
    DEPS zlib
)
# INSANE was built with /Zp1. -fpack-struct=1 is not used: it would also repack system structs (FILE, pthread types)
# seen through its headers. Structures that depend on 1-byte packing are audited in Phase 5 (docs/PORTING.md).

j3d_library(sith
    DIRS Libs/sith
    EXCLUDE
        Libs/sith/World/sithWrite.c     # debug-build-only level writer, outside the release link (docs/SOURCE-TREE.md)
    DEPS std rdroid sound
)

# ---------------------------------------------------------------------------------------------------------------------
# Platform layer. Phase 0 provides the null platform: every Windows-facing function exists and fails or does nothing,
# so the tree links and the engine can start up to the point where it opens a display.
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB J3D_PLATFORM_SOURCES CONFIGURE_DEPENDS
    ${J3D_ROOT}/platform/common/*.c         # real implementations (CRT names, file search)
    ${J3D_ROOT}/platform/common/null/*.c    # null backends, replaced phase by phase
)
add_library(platform STATIC ${J3D_PLATFORM_SOURCES})
target_link_libraries(platform PUBLIC j3d_common)

# ---------------------------------------------------------------------------------------------------------------------
# The game: Jones3D shell + the app entry point.
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE J3D_GAME_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/Jones3D/*.c)
add_executable(jones3d
    ${J3D_GAME_SOURCES}
    ${J3D_ROOT}/platform/apple/main.c
)
target_link_libraries(jones3d PRIVATE
    j3d_common
    sith smush sound rdroid std zlib
    platform
)
set_target_properties(jones3d PROPERTIES
    OUTPUT_NAME jones3d
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY ${J3D_ROOT}
)
# The static libraries reference each other in cycles (sith <-> std <-> platform); let the linker resolve them.
target_link_options(jones3d PRIVATE LINKER:-no_warn_duplicate_libraries)
