cmake_minimum_required(VERSION 3.24)

# Port of the Jones3D engine (infernal-machine-decomp, upstream commit 319a6fc2) to arm64 macOS.
# See docs/PORT-PLAN.md for the phases and docs/PORTING.md for every deviation from the decomp.
project(OpenIndy3D VERSION 1.0.0 LANGUAGES C)

# Which platform backends the game binary uses (PORT-PLAN.md P2.1):
#   apple  SDL3 window and input, Metal renderer; Objective-C, SDL3 and the Apple frameworks are required.
#   null   the generated null backends only (no window, no renderer), so the tree builds without SDL3.
# The test programs, and the headless binary the boot test runs, link the null backends in either case.
set(J3D_PLATFORM apple CACHE STRING "Platform backends for the game binary: apple or null")
set_property(CACHE J3D_PLATFORM PROPERTY STRINGS apple null)
if(NOT J3D_PLATFORM MATCHES "^(apple|null)$")
    message(FATAL_ERROR "J3D_PLATFORM must be apple or null, not '${J3D_PLATFORM}'")
endif()
if(J3D_PLATFORM STREQUAL "apple")
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)
endif()
# The product (PORT-PLAN.md P8.1): the app's name is J3D_PRODUCT_NAME in platform/common/host_product.h.
set(J3D_PRODUCT_NAME OpenIndy3D)
set(J3D_BUNDLE_ID "io.github.jenavieve-rose.OpenIndy3D" CACHE STRING "The app's bundle identifier (PORT-PLAN.md P8.1)")

if(J3D_PLATFORM STREQUAL "apple")
    # SDL3 (PORT-PLAN.md P8.3). By default a pinned release is built here as a static library for the deployment
    # target, so the app loads nothing from outside its bundle and runs on macOS 13; Homebrew's builds require the
    # macOS they were built on. J3D_SYSTEM_SDL3=ON uses an installed SDL3 instead (find_package), for a quicker first
    # configure; such a build is not for distribution.
    option(J3D_SYSTEM_SDL3 "Link an installed SDL3 instead of building the pinned release (not for distribution)" OFF)
    if(J3D_SYSTEM_SDL3)
        find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3-shared)
    else()
        include(FetchContent)
        set(J3D_SDL3_URL "https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz"
            CACHE STRING "The SDL3 source release to build (a URL or a local tarball)")
        FetchContent_Declare(SDL3
            URL ${J3D_SDL3_URL}
            URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            EXCLUDE_FROM_ALL)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
    endif()

    # The menus are SwiftUI (PORT-PLAN.md Phase 6, decision 20). Enabled here so Swift takes the deployment target.
    set(CMAKE_Swift_LANGUAGE_VERSION 5)
    enable_language(Swift)
endif()

# J3D_DEBUG (j3d.h) follows NDEBUG. The decomp's release build is the reference, so NDEBUG is defined in every
# configuration unless the engine's debug asserts are asked for explicitly.
option(J3D_WERROR_SHORTEN "Treat -Wshorten-64-to-32 as an error (on since the Phase 1 audit closed, PORT-PLAN.md P1.29)" ON)
option(J3D_DEBUG_ASSERTS "Compile the engine's debug-only J3D_ASSERT checks" OFF)
option(J3D_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (PORT-PLAN.md P1.21)" OFF)
# OpenJones3D's fixes to retail bugs (PORT-PLAN.md P8.9, docs/UPSTREAM-FIXES.md): crashes, memory errors, COG and
# sound-handle bugs and raised limits, each marked "Fix (OpenJones3D <commit>)" under #ifdef J3D_FIXES. OFF builds
# retail's behaviour, for comparisons with the Windows build.
option(J3D_FIXES "Apply OpenJones3D's fixes to retail bugs (PORT-PLAN.md P8.9)" ON)

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
    J3D_PRODUCT_VERSION="${PROJECT_VERSION}"   # the app's version (host_product.h)
)
if(NOT J3D_DEBUG_ASSERTS)
    target_compile_definitions(j3d_common INTERFACE NDEBUG)
endif()
if(J3D_FIXES)
    target_compile_definitions(j3d_common INTERFACE J3D_FIXES=1)
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
    # A function that falls off its end returned whatever eax held in retail and returns garbage here. All known cases
    # are fixed (docs/PORTING.md, "Functions with no return value"); a new one must not get in.
    -Werror=return-type
    # Every 64-to-32 narrowing is reported, and is an error while J3D_WERROR_SHORTEN is on (the default). The Phase 1
    # audit triaged all of them (docs/PORTING.md, "64-bit audit: narrowing conversions"); jonesConfig.c is exempt below.
    -Wshorten-64-to-32
    # Every other warning class was cleared in Phase 1 (PORT-PLAN.md P1.23, P1.24, P1.28) and is an error from then on.
    # The exception: narrowing until J3D_WERROR_SHORTEN is on. (AudioLib.c.s unsequenced write, the other exception
    # until then, was fixed in P4.8.)
    -Werror
    $<$<NOT:$<BOOL:${J3D_WERROR_SHORTEN}>>:-Wno-error=shorten-64-to-32>
    # The decomp is written in the MSVC dialect; these fire on idioms that are correct there.
    -Wno-parentheses
    -Wno-dangling-else
    -Wno-switch
    -Wno-pointer-sign
    -Wno-missing-braces
    -Wno-unused-value
    -Wno-comment
    -Wno-microsoft-anon-tag
    -Wno-deprecated-declarations   # sprintf and friends, deprecated by the macOS SDK; reported once optimisation is on
)

# Sanitizer build: every target, the tests included, is compiled and linked with ASan and UBSan. Configure it in its
# own build directory (docs/BUILDING.md). UBSan reports are recoverable in the binaries, so a manual run with
# UBSAN_OPTIONS=halt_on_error=0 lists every report; the tests set halt_on_error=1 so the first one fails them.
if(J3D_SANITIZE)
    target_compile_options(j3d_common INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(j3d_common INTERFACE -fsanitize=address,undefined)
    set(J3D_SANITIZER_ENV "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=detect_leaks=0")
endif()

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
        Libs/smush/insane/Timer/Timer.c     # Win32 thread         -> platform/common/insane_timer.c (P5.3)
        Libs/smush/insane/Sound/Dsnd.c      # DirectSound          -> platform/common/insane_dsnd.c (P5.9)
    DEPS zlib
)
# INSANE was built with /Zp1. -fpack-struct=1 is not used: it would also repack system structs (FILE, pthread types)
# seen through its headers. The structures that depend on 1-byte packing (the chunk layouts read from movie files) are
# packed explicitly with their sizes asserted (P5.1, docs/PORTING.md). Its six MASM files are C beside them (P5.4-P5.7).

# sithWrite.c is the debug build's level writer, outside the release link (docs/SOURCE-TREE.md). It is built with the
# engine's debug asserts, whose template writer calls it (sithTemplate.c, PORT-PLAN.md P7.3).
if(J3D_DEBUG_ASSERTS)
    set(J3D_SITH_EXCLUDE)
else()
    set(J3D_SITH_EXCLUDE Libs/sith/World/sithWrite.c)
endif()
j3d_library(sith
    DIRS Libs/sith
    EXCLUDE ${J3D_SITH_EXCLUDE}
    DEPS std rdroid sound
)

# ---------------------------------------------------------------------------------------------------------------------
# Platform layer, as three libraries:
#   platform_core   real implementations every build shares (CRT names, paths, settings, VBuffers, display queries
#                   and back buffer, the sound driver, INSANE's timer and audio, and j3dgui: the Win32 dialog emulation)
#   platform_null   the generated null backends: every Windows-facing function exists and fails or does nothing;
#                   plus the headless display device (stdDisplay_headless.c)
#   platform_apple  the SDL3 and Metal backends (J3D_PLATFORM=apple), plus the null backends they do not yet replace
# The engine libraries call into the platform and the platform calls back into them, so the platform is not a
# dependency of jones3d_game: each executable links jones3d_game and exactly one of platform_null and platform_apple.
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB J3D_PLATFORM_CORE_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/platform/common/*.c ${J3D_ROOT}/platform/common/j3dgui/*.c)
add_library(platform_core STATIC ${J3D_PLATFORM_CORE_SOURCES})
target_link_libraries(platform_core PUBLIC j3d_common)

file(GLOB J3D_PLATFORM_NULL_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/platform/common/null/*.c)
add_library(platform_null STATIC ${J3D_PLATFORM_NULL_SOURCES})
target_link_libraries(platform_null PUBLIC platform_core)

if(J3D_PLATFORM STREQUAL "apple")
    # Null backend files that an Apple backend replaces whole. A backend that replaces only part of a null file is
    # handled in tools/gen_null_platform.py instead, by moving the rest into a file of its own.
    set(J3D_APPLE_REPLACES_NULL
        wkernel_null.c          # platform/apple/wkernel_sdl.c (P2.2)
        win32_msg_null.c        # the game window's message queue and timers, also wkernel_sdl.c
        stdDisplay_headless.c   # platform/apple/stdDisplay_sdl.c (P2.3)
        std3D_null.c            # platform/apple/std3D_metal.c and render_metal.m (P2.9)
        stdControl_null.c       # platform/apple/stdControl_sdl.c (Phase 3)
        audio_device_null.c     # platform/apple/audio_device_avf.m (Phase 4, decision 19)
        gui_text_null.c         # platform/apple/gui_text_ct.m, the dialogs' text through Core Text (Phase 6)
        j3dmenu_null.c          # platform/apple/j3dmenu_apple.c and menu/, the SwiftUI menus (Phase 6)
    )
    set(J3D_APPLE_NULL_SOURCES ${J3D_PLATFORM_NULL_SOURCES})
    foreach(replaced IN LISTS J3D_APPLE_REPLACES_NULL)
        list(REMOVE_ITEM J3D_APPLE_NULL_SOURCES ${J3D_ROOT}/platform/common/null/${replaced})
    endforeach()

    # platform/apple/ holds the backends; main.c (the entry point) and shim/ (Win32 type headers) are not part of it.
    file(GLOB J3D_PLATFORM_APPLE_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/platform/apple/*.c ${J3D_ROOT}/platform/apple/*.m)
    list(REMOVE_ITEM J3D_PLATFORM_APPLE_SOURCES ${J3D_ROOT}/platform/apple/main.c)

    add_library(platform_apple STATIC ${J3D_PLATFORM_APPLE_SOURCES} ${J3D_APPLE_NULL_SOURCES})
    target_link_libraries(platform_apple PUBLIC
        platform_core
        SDL3::SDL3
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework AVFAudio"
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
    target_compile_definitions(platform_apple PUBLIC J3D_PLATFORM_APPLE=1)
    # Objective-C files are compiled with ARC. The C-only warning switches in j3d_common apply to them unchanged.
    target_compile_options(platform_apple PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)

    # platform/apple/menu/: the SwiftUI menus, a Swift library of its own. j3dmenu_ui.h is its bridging header and the
    # whole of its interface with the C code (j3dmenu_apple.c); J3DMENU_SWIFT hides the prototypes Swift implements.
    file(GLOB J3D_MENU_SWIFT_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/platform/apple/menu/*.swift)
    add_library(j3dmenu_swift STATIC ${J3D_MENU_SWIFT_SOURCES})
    set_target_properties(j3dmenu_swift PROPERTIES Swift_MODULE_NAME J3DMenu)
    target_compile_options(j3dmenu_swift PRIVATE
        -parse-as-library
        -target ${CMAKE_OSX_ARCHITECTURES}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}
        -import-objc-header ${J3D_ROOT}/platform/apple/menu/j3dmenu_ui.h
        -Xcc -DJ3DMENU_SWIFT
        -warnings-as-errors)
    # Everything else is C, archived and linked as C: the Swift objects name the runtime libraries they need
    # themselves, and the linker only has to be told where the SDK and the toolchain keep them.
    # The toolchain's own swiftc, not /usr/bin's shim, whose folder has no Swift libraries beside it
    execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE J3D_SWIFTC OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(J3D_SWIFT_BIN_DIR ${J3D_SWIFTC} DIRECTORY)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE J3D_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_libraries(j3dmenu_swift PUBLIC "-framework SwiftUI" "-framework AppKit")
    target_link_options(j3dmenu_swift PUBLIC
        -L${J3D_MACOS_SDK}/usr/lib/swift -L${J3D_SWIFT_BIN_DIR}/../lib/swift/macosx LINKER:-rpath,/usr/lib/swift)
    target_link_libraries(platform_apple PUBLIC j3dmenu_swift)
    set_target_properties(platform_apple PROPERTIES LINKER_LANGUAGE C)
endif()

# ---------------------------------------------------------------------------------------------------------------------
# The game: Jones3D shell + the app entry point.
# ---------------------------------------------------------------------------------------------------------------------
# The game shell is a library so the test harnesses can link it without the app entry point.
file(GLOB_RECURSE J3D_GAME_SOURCES CONFIGURE_DEPENDS ${J3D_ROOT}/Jones3D/*.c)
add_library(jones3d_game STATIC ${J3D_GAME_SOURCES})
target_link_libraries(jones3d_game PUBLIC
    j3d_common
    sith smush sound rdroid std zlib
)

# j3d_executable(<target> <platform library> <sources...>): a program linked with the game and one platform.
function(j3d_executable name platform)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
    target_link_libraries(${name} PRIVATE jones3d_game ${platform})
    # The static libraries reference each other in cycles (sith <-> std <-> platform); let the linker resolve them.
    target_link_options(${name} PRIVATE LINKER:-no_warn_duplicate_libraries)
endfunction()

j3d_executable(jones3d platform_${J3D_PLATFORM} ${J3D_ROOT}/platform/apple/main.c)
set_target_properties(jones3d PROPERTIES
    OUTPUT_NAME jones3d
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY ${J3D_ROOT}
)

# In an apple build the game is the app (PORT-PLAN.md P8.2): <build>/OpenIndy3D.app, with jones3d beside it as a link
# to its executable, for the tools and for runs from a terminal. The null build's jones3d stays a plain program.
if(J3D_PLATFORM STREQUAL "apple")
    # The icon is made from a square picture (tools/make_icns.sh). The default is the box art kept in the repository,
    # platform/apple/bundle/; J3D_ICON_SOURCE names another picture, or is empty for no icon.
    set(J3D_ICON_DEFAULT "${J3D_ROOT}/platform/apple/bundle/indiana_jones_and_the_infernal_machine-square.jpg")
    set(J3D_ICON_SOURCE "${J3D_ICON_DEFAULT}" CACHE FILEPATH "Square picture (1024 by 1024 or more) the app icon is made from; empty for none")
    if(J3D_ICON_SOURCE)
        set(J3D_ICNS ${CMAKE_CURRENT_BINARY_DIR}/${J3D_PRODUCT_NAME}.icns)
        add_custom_command(OUTPUT ${J3D_ICNS}
            COMMAND sh ${J3D_ROOT}/tools/make_icns.sh ${J3D_ICON_SOURCE} ${J3D_ICNS}
            DEPENDS ${J3D_ICON_SOURCE} ${J3D_ROOT}/tools/make_icns.sh
            COMMENT "Making the app icon")
        set_source_files_properties(${J3D_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(jones3d PRIVATE ${J3D_ICNS})
        set(J3D_ICON_FILE ${J3D_PRODUCT_NAME}.icns)
    else()
        set(J3D_ICON_FILE "")
    endif()

    # The About window's text and the license and notices, in the app (P8.12)
    set(J3D_BUNDLE_DOCS ${J3D_ROOT}/platform/apple/bundle/Credits.html ${J3D_ROOT}/LICENSE ${J3D_ROOT}/NOTICE)
    set_source_files_properties(${J3D_BUNDLE_DOCS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
    target_sources(jones3d PRIVATE ${J3D_BUNDLE_DOCS})

    set_target_properties(jones3d PROPERTIES
        OUTPUT_NAME ${J3D_PRODUCT_NAME}
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${J3D_ROOT}/platform/apple/bundle/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME ${J3D_PRODUCT_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER ${J3D_BUNDLE_ID}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "${J3D_ICON_FILE}"
        MACOSX_BUNDLE_COPYRIGHT "Free software under the GNU AGPL v3. A retail copy of the game is required."
    )
    add_custom_command(TARGET jones3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${J3D_PRODUCT_NAME}.app/Contents/MacOS/${J3D_PRODUCT_NAME}
                ${CMAKE_CURRENT_BINARY_DIR}/jones3d
        VERBATIM)
endif()

# The headless game binary for the boot test: jones3d itself in a null build, a null-backend twin in an apple build.
if(J3D_PLATFORM STREQUAL "null")
    set(J3D_HEADLESS_TARGET jones3d)
else()
    j3d_executable(jones3d-null platform_null ${J3D_ROOT}/platform/apple/main.c)
    set(J3D_HEADLESS_TARGET jones3d-null)
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Tests (PORT-PLAN.md Phase 1). They need the player's game data: configure with -DJ3D_TEST_DATA=<folder holding
# RESOURCE/>, then run ctest. Without it the targets still build but no tests are registered.
# ---------------------------------------------------------------------------------------------------------------------
set(J3D_TEST_DATA "" CACHE PATH "Game data root (the folder holding RESOURCE/) for the tests")

# Each test is one program: tests/<name>.c plus the shared engine startup and crash handlers in tests/harness.c.
#   levelload  loads the static world and every level through the binary .cnd section loaders
#   cogcheck   compiles every .cog in the GOBs through the engine's lookup (loose overrides included)
#   saveload   runs the game headless, saves, restores into a fresh world and compares (below for its settings);
#              registered once for the first level and once for each of the others (P7.2)
#   boot       (below) runs the game headless
set(J3D_TESTS levelload cogcheck saveload)

enable_testing()
foreach(test IN LISTS J3D_TESTS)
    j3d_executable(${test} platform_null ${J3D_ROOT}/tests/${test}.c ${J3D_ROOT}/tests/harness.c)
    if(J3D_TEST_DATA)
        add_test(NAME ${test} COMMAND ${test} ${J3D_TEST_DATA})
        set_tests_properties(${test} PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
    endif()
endforeach()

# Tests that need no game data, registered in every configuration:
#   display    the shared system-memory half of stdDisplay: back buffer, fills, blits, RGB565, SaveScreen (P2.4 to P2.7)
#   present    (apple builds) the Metal present path, read back from a real window; skipped (77) with no window server
#   render     (apple builds) the Metal renderer's shaders and fixed-function state, offscreen (P2.10); render-msaa
#              and render-managed run it multisampled and with an Intel Mac's managed textures (phase 9)
#   audio3d    (apple builds) the audio device places 3D voices on the right side; skipped (77) with no output device
#   keys       the DIK tables and GetKeyNameTextA and SearchPathA built on them (P3.1, P3.7)
#   sounddriver the sound driver on the virtual clock, and its mix, rendered without a device (P4.3 to P4.6)
#   insaneasm  the C translations of INSANE's assembler helpers against models of the assembler (P5.4 to P5.6)
j3d_executable(display platform_null ${J3D_ROOT}/tests/display.c ${J3D_ROOT}/tests/harness.c)
add_test(NAME display COMMAND display)
set_tests_properties(display PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
j3d_executable(keys platform_null ${J3D_ROOT}/tests/keys.c)
add_test(NAME keys COMMAND keys)

# datacheck: the game data check (P8.5); with the test data, that it is a tested release
j3d_executable(datacheck platform_null ${J3D_ROOT}/tests/datacheck.c)
add_test(NAME datacheck COMMAND datacheck ${J3D_TEST_DATA})
set_tests_properties(keys PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
j3d_executable(sounddriver platform_null ${J3D_ROOT}/tests/sounddriver.c)
add_test(NAME sounddriver COMMAND sounddriver)
set_tests_properties(sounddriver PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
# dialogs: the Win32 dialog emulation on the game's templates, driven through input (Phase 6). The apple build links the
# apple platform, so its BMP dumps (DIALOGS_DUMP) show Core Text; with game data its icons must come from Indy3D.exe.
j3d_executable(dialogs platform_${J3D_PLATFORM} ${J3D_ROOT}/tests/dialogs.c ${J3D_ROOT}/tests/harness.c)
if(J3D_TEST_DATA)
    add_test(NAME dialogs COMMAND dialogs ${J3D_TEST_DATA})
else()
    add_test(NAME dialogs COMMAND dialogs)
endif()
set_tests_properties(dialogs PROPERTIES ENVIRONMENT "J3D_SETTINGS=/dev/null;${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
j3d_executable(insaneasm platform_null ${J3D_ROOT}/tests/insaneasm.c)
add_test(NAME insaneasm COMMAND insaneasm)
set_tests_properties(insaneasm PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
if(J3D_PLATFORM STREQUAL "apple")
    j3d_executable(present platform_apple ${J3D_ROOT}/tests/present.c ${J3D_ROOT}/tests/harness.c)
    add_test(NAME present COMMAND present)
    set_tests_properties(present PROPERTIES
        ENVIRONMENT "${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT" SKIP_RETURN_CODE 77)
    j3d_executable(audio3d platform_apple ${J3D_ROOT}/tests/audio3d.c)
    add_test(NAME audio3d COMMAND audio3d ${CMAKE_CURRENT_BINARY_DIR}/audio3d.wav)
    set_tests_properties(audio3d PROPERTIES ENVIRONMENT "J3D_SETTINGS=/dev/null;${J3D_SANITIZER_ENV}" SKIP_RETURN_CODE 77)
    j3d_executable(render platform_apple ${J3D_ROOT}/tests/render.c ${J3D_ROOT}/tests/harness.c)
    add_test(NAME render COMMAND render)
    set_tests_properties(render PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
    # Phase 9: the same checks multisampled with anisotropic samplers, and with managed textures (Intel Macs)
    add_test(NAME render-msaa COMMAND render --samples 4 --anisotropy 16)
    set_tests_properties(render-msaa PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
    add_test(NAME render-managed COMMAND render --samples 4)
    set(J3D_MANAGED_ENV ${J3D_SANITIZER_ENV} J3D_METAL_MANAGED=1)
    set_tests_properties(render-managed PROPERTIES ENVIRONMENT "${J3D_MANAGED_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
endif()

# boot: the game itself (its null-backend build, J3D_HEADLESS_TARGET), headless (in-memory display, virtual clock), for
# 600 frames of the first level: 20 seconds of game time, which covers level open, the level-start autosave and the
# start of the opening cutscene. It must exit 0. The settings file is not touched, and "Install Path" (SaveGames/,
# ScreenShots/, KeySets/) and the log folder point into the build tree, so the run writes nothing under the user's Library. Paths up to J3D_PATHSIZE (1024) bytes work
# (PORT-PLAN.md P1.30).
# saveload writes its two saves under an "Install Path" in the build tree, like boot.
if(J3D_TEST_DATA)
    set_tests_properties(saveload PROPERTIES
        ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Debug_Mode=1;J3D_REG_Verbosity=2;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/saveload-out/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/saveload-out/logs/;${J3D_SANITIZER_ENV}"
        FAIL_REGULAR_EXPRESSION "ASSERT")
    # saveload-3d: the same with "Sound 3D" on (decision 19), so 3D voices' channel flags go through the round trip.
    add_test(NAME saveload-3d COMMAND saveload ${J3D_TEST_DATA})
    set_tests_properties(saveload-3d PROPERTIES
        ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Sound_3D=1;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/saveload-3d-out/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/saveload-3d-out/logs/;${J3D_SANITIZER_ENV}"
        FAIL_REGULAR_EXPRESSION "ASSERT")
    # saveload-<nn>_<level>: the same round trip for each of the other sixteen levels (PORT-PLAN.md P7.2), numbered as
    # the game numbers them (JonesMain_aCndLevelLoadInfos), each with a save folder of its own so they can run together.
    set(J3D_LEVELS 00_cyn 01_bab 02_riv 03_shs 05_lag 06_vol 07_tem 16_jep 08_teo 09_olv 10_sea 11_pyr 12_sol 13_nub
        14_inf 15_aet 17_pru)
    set(level_num 0)
    foreach(level IN LISTS J3D_LEVELS)
        math(EXPR level_num "${level_num} + 1")
        if(level_num GREATER 1)
            string(LENGTH "0${level_num}" len)
            math(EXPR start "${len} - 2")
            string(SUBSTRING "0${level_num}" ${start} 2 nn)
            set(test saveload-${nn}_${level})
            add_test(NAME ${test} COMMAND saveload ${J3D_TEST_DATA} 300 300 ${level_num})
            set_tests_properties(${test} PROPERTIES
                ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/saveload-out/${nn}/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/saveload-out/${nn}/logs/;${J3D_SANITIZER_ENV}"
                FAIL_REGULAR_EXPRESSION "ASSERT")
        endif()
    endforeach()
endif()

# outfit: Indy's outfit, the "Indy Outfit" setting (docs/PORTING.md). Starts a level with an outfit, checks the player's
# and Indy's templates' models, then restores a save of it with each outfit and checks again. Three levels: 1 (Canyonlands,
# jacketless in retail) started with the jacket, 3 (Tian Shan River, which has no jacketless model) started without,
# and 17 (Peru, both raft models too) started without.
j3d_executable(outfit platform_null ${J3D_ROOT}/tests/outfit.c ${J3D_ROOT}/tests/harness.c)
if(J3D_TEST_DATA)
    foreach(run IN ITEMS "01_cyn;1;1" "03_riv;3;2" "17_pru;17;2")
        list(GET run 0 level)
        list(GET run 1 level_num)
        list(GET run 2 start_outfit)
        set(test outfit-${level})
        add_test(NAME ${test} COMMAND outfit ${J3D_TEST_DATA} ${level_num} 300)
        set_tests_properties(${test} PROPERTIES
            ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Indy_Outfit=${start_outfit};J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/outfit-out/${level}/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/outfit-out/${level}/logs/;${J3D_SANITIZER_ENV}"
            FAIL_REGULAR_EXPRESSION "ASSERT")
    endforeach()
endif()

# gamedialogs: the game's own dialogs through their entry points, with the player's data (Phase 6).
j3d_executable(gamedialogs platform_null ${J3D_ROOT}/tests/gamedialogs.c ${J3D_ROOT}/tests/harness.c)
if(J3D_TEST_DATA)
    add_test(NAME gamedialogs COMMAND gamedialogs ${J3D_TEST_DATA})
    set_tests_properties(gamedialogs PROPERTIES
        ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/gamedialogs-out/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/gamedialogs-out/logs/;${J3D_SANITIZER_ENV}"
        FAIL_REGULAR_EXPRESSION "ASSERT")
endif()

# moviecheck: every movie in the data decoded headlessly, every frame shown, codec 2 checked against ScummVM's
# Blocky16, frame and audio checksums (PORT-PLAN.md P5.8). It reads only the movie files.
j3d_executable(moviecheck platform_null ${J3D_ROOT}/tests/moviecheck.c ${J3D_ROOT}/tests/harness.c)
if(J3D_TEST_DATA)
    add_test(NAME moviecheck COMMAND moviecheck ${J3D_TEST_DATA})
    set_tests_properties(moviecheck PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" FAIL_REGULAR_EXPRESSION "ASSERT")
endif()

# boot-sdl (apple builds): the same run through the apple jones3d, so the SDL3 event loop (wkernel_sdl.c), with SDL's
# dummy video driver, delivers the frames and the closing WM_CLOSE.
if(J3D_TEST_DATA AND J3D_PLATFORM STREQUAL "apple")
    add_test(NAME boot-sdl COMMAND jones3d --data ${J3D_TEST_DATA} --headless --frames 600)
    set_tests_properties(boot-sdl PROPERTIES
        ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Debug_Mode=1;J3D_REG_Verbosity=2;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/boot-sdl/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/boot-sdl/logs/;${J3D_SANITIZER_ENV}"
        FAIL_REGULAR_EXPRESSION "ASSERT")
endif()

# input (apple builds): the Phase 3 input path end to end, through scripted input, recording and replay; see
# tests/input.sh. Its runs write under the build tree only.
if(J3D_TEST_DATA AND J3D_PLATFORM STREQUAL "apple")
    add_test(NAME input COMMAND sh ${J3D_ROOT}/tests/input.sh $<TARGET_FILE:jones3d> ${J3D_TEST_DATA}
        ${CMAKE_CURRENT_BINARY_DIR}/input-out)
    set_tests_properties(input PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
    # launcher: Start New Game, Resume Play and Options, answered without clicks (tests/launcher.sh)
    add_test(NAME launcher COMMAND sh ${J3D_ROOT}/tests/launcher.sh $<TARGET_FILE:jones3d> ${J3D_TEST_DATA}
        ${CMAKE_CURRENT_BINARY_DIR}/launcher-out)
    set_tests_properties(launcher PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" SKIP_RETURN_CODE 77)
endif()

# determinism (Phase 7 audit): every level started twice headless must print the same log and write the same autosave.
if(J3D_TEST_DATA)
    add_test(NAME determinism COMMAND sh ${J3D_ROOT}/tests/determinism.sh $<TARGET_FILE:${J3D_HEADLESS_TARGET}>
        ${J3D_TEST_DATA} ${CMAKE_CURRENT_BINARY_DIR}/determinism-out)
    set_tests_properties(determinism PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}")
endif()

if(J3D_TEST_DATA)
    add_test(NAME boot COMMAND ${J3D_HEADLESS_TARGET} --data ${J3D_TEST_DATA} --headless --frames 600)
    # crash: a crash writes Crash.log and the next run says where it is (P8.11, tests/crash.sh)
    add_test(NAME crash COMMAND sh ${J3D_ROOT}/tests/crash.sh $<TARGET_FILE:${J3D_HEADLESS_TARGET}> ${J3D_TEST_DATA}
        ${CMAKE_CURRENT_BINARY_DIR}/crash-out)
    set_tests_properties(crash PROPERTIES ENVIRONMENT "${J3D_SANITIZER_ENV}" SKIP_RETURN_CODE 77)
    set_tests_properties(boot PROPERTIES
        ENVIRONMENT "J3D_SETTINGS=/dev/null;J3D_REG_Start_Mode=0;J3D_REG_Debug_Mode=1;J3D_REG_Verbosity=2;J3D_REG_Install_Path=${CMAKE_CURRENT_BINARY_DIR}/boot/;J3D_LOGS=${CMAKE_CURRENT_BINARY_DIR}/boot/logs/;${J3D_SANITIZER_ENV}"
        FAIL_REGULAR_EXPRESSION "ASSERT")
endif()
