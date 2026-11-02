# Cross-platform discovery of the external libraries ISACPlayer links against.
#
# Linux distributions ship TagLib and FFTW3 with pkg-config metadata. Windows
# builds normally obtain them from vcpkg (or another prefix that installs CMake
# package configs), so pkg-config cannot be a hard requirement any more.
#
# Every dependency is normalised into an interface target (isac_taglib,
# isac_fftw3, isac_mysofa) so targets do not need to know how it was located.
# The raw ISAC_MYSOFA_* variables are kept because plugin and test targets use
# them directly for optional compile definitions.

include_guard(GLOBAL)

find_package(PkgConfig QUIET)

# --------------------------------------------------------------------- TagLib
set(ISAC_TAGLIB_TARGET "")
set(ISAC_TAGLIB_INCLUDE_DIR "")

if(WIN32)
    # Prefer the CMake package that vcpkg installs (lowercase package name,
    # exported C++ target TagLib::tag).
    find_package(taglib CONFIG QUIET)
    if(TARGET TagLib::tag)
        set(ISAC_TAGLIB_TARGET TagLib::tag)
    elseif(TARGET TagLib::TagLib)
        set(ISAC_TAGLIB_TARGET TagLib::TagLib)
    endif()
endif()

if(NOT ISAC_TAGLIB_TARGET AND PkgConfig_FOUND)
    pkg_check_modules(ISAC_TAGLIB_PC QUIET IMPORTED_TARGET taglib)
    if(ISAC_TAGLIB_PC_FOUND)
        set(ISAC_TAGLIB_TARGET PkgConfig::ISAC_TAGLIB_PC)
    endif()
endif()

if(NOT ISAC_TAGLIB_TARGET)
    find_package(taglib CONFIG QUIET)
    if(TARGET TagLib::tag)
        set(ISAC_TAGLIB_TARGET TagLib::tag)
    endif()
endif()

if(NOT ISAC_TAGLIB_TARGET)
    find_path(ISAC_TAGLIB_INCLUDE_DIR NAMES taglib/taglib.h)
    find_library(ISAC_TAGLIB_LIBRARY NAMES taglib tag)
    if(ISAC_TAGLIB_INCLUDE_DIR AND ISAC_TAGLIB_LIBRARY)
        set(ISAC_TAGLIB_TARGET "${ISAC_TAGLIB_LIBRARY}")
    endif()
endif()

if(NOT ISAC_TAGLIB_TARGET)
    message(FATAL_ERROR
        "TagLib was not found.\n"
        "  Linux: install the TagLib development package (for example "
        "'taglib' on Arch Linux).\n"
        "  Windows: configure with "
        "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake after "
        "'vcpkg install taglib fftw3 libmysofa', or pass "
        "-DCMAKE_PREFIX_PATH=<prefix>.")
endif()

add_library(isac_taglib INTERFACE)
target_link_libraries(isac_taglib INTERFACE ${ISAC_TAGLIB_TARGET})
if(ISAC_TAGLIB_INCLUDE_DIR)
    target_include_directories(isac_taglib INTERFACE ${ISAC_TAGLIB_INCLUDE_DIR})
endif()
message(STATUS "TagLib: ${ISAC_TAGLIB_TARGET}")

# ---------------------------------------------------------------------- FFTW3
set(ISAC_FFTW3_TARGET "")
set(ISAC_FFTW3_INCLUDE_DIR "")

if(WIN32)
    find_package(FFTW3 CONFIG QUIET)
    if(TARGET FFTW3::fftw3)
        set(ISAC_FFTW3_TARGET FFTW3::fftw3)
    endif()
endif()

if(NOT ISAC_FFTW3_TARGET AND PkgConfig_FOUND)
    pkg_check_modules(ISAC_FFTW3_PC QUIET IMPORTED_TARGET fftw3)
    if(ISAC_FFTW3_PC_FOUND)
        set(ISAC_FFTW3_TARGET PkgConfig::ISAC_FFTW3_PC)
    endif()
endif()

if(NOT ISAC_FFTW3_TARGET)
    find_package(FFTW3 CONFIG QUIET)
    if(TARGET FFTW3::fftw3)
        set(ISAC_FFTW3_TARGET FFTW3::fftw3)
    endif()
endif()

if(NOT ISAC_FFTW3_TARGET)
    # vcpkg names the import library fftw3.lib; some MinGW packages use the
    # upstream libfftw3-3 name.
    find_path(ISAC_FFTW3_INCLUDE_DIR NAMES fftw3.h)
    find_library(ISAC_FFTW3_LIBRARY NAMES fftw3 libfftw3-3 fftw3-3)
    if(ISAC_FFTW3_INCLUDE_DIR AND ISAC_FFTW3_LIBRARY)
        set(ISAC_FFTW3_TARGET "${ISAC_FFTW3_LIBRARY}")
    endif()
endif()

if(NOT ISAC_FFTW3_TARGET)
    message(FATAL_ERROR
        "FFTW3 was not found.\n"
        "  Linux: install the FFTW development package (for example 'fftw').\n"
        "  Windows: 'vcpkg install fftw3' and configure with the vcpkg "
        "toolchain file, or pass -DCMAKE_PREFIX_PATH=<prefix>.")
endif()

add_library(isac_fftw3 INTERFACE)
target_link_libraries(isac_fftw3 INTERFACE ${ISAC_FFTW3_TARGET})
if(ISAC_FFTW3_INCLUDE_DIR)
    target_include_directories(isac_fftw3 INTERFACE ${ISAC_FFTW3_INCLUDE_DIR})
endif()
# Mixing single-precision and double-precision FFTW is a link-time trap on
# Windows, where separate import libraries exist for each precision.
if(WIN32)
    target_compile_definitions(isac_fftw3 INTERFACE FFTW_DLL)
endif()
message(STATUS "FFTW3: ${ISAC_FFTW3_TARGET}")

# ------------------------------------------------------------------- libmysofa
# Optional: the HRTF spatializer plugin is only built when libmysofa exists.
set(ISAC_MYSOFA_INCLUDE_DIR "")
set(ISAC_MYSOFA_LIBRARY "")
find_path(ISAC_MYSOFA_INCLUDE_DIR NAMES mysofa.h)
find_library(ISAC_MYSOFA_LIBRARY NAMES mysofa libmysofa)
if(ISAC_MYSOFA_INCLUDE_DIR AND ISAC_MYSOFA_LIBRARY)
    add_library(isac_mysofa INTERFACE)
    target_include_directories(isac_mysofa INTERFACE ${ISAC_MYSOFA_INCLUDE_DIR})
    target_link_libraries(isac_mysofa INTERFACE ${ISAC_MYSOFA_LIBRARY})
    message(STATUS "libmysofa: ${ISAC_MYSOFA_LIBRARY}")
else()
    message(STATUS "libmysofa not found: Stereo HRTF Spatializer plugin disabled")
endif()
