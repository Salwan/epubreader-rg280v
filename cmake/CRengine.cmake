# Headless subset of the pinned KOReader build. Explicit features prevent host
# packages from silently enabling a different renderer from the device build.
set(CRE_ROOT "${PROJECT_SOURCE_DIR}/third_party/src/crengine")
set(XXHASH_ROOT "${PROJECT_SOURCE_DIR}/third_party/src/xxhash")
if(NOT EXISTS "${CRE_ROOT}/crengine/include/crsetup.h.cmake" OR NOT EXISTS "${XXHASH_ROOT}/xxhash.c")
    message(FATAL_ERROR "Run python3 scripts/bootstrap.py --host (host) or scripts/bootstrap.py (device) first")
endif()
pkg_check_modules(RENDER REQUIRED IMPORTED_TARGET freetype2 zlib libpng libjpeg)
add_library(reader_hash STATIC "${XXHASH_ROOT}/xxhash.c")
target_include_directories(reader_hash PUBLIC "${XXHASH_ROOT}")
foreach(feature ZSTD CHMLIB ANTIWORD MD4C GIF LIBWEBP LUNASVG NANOSVG STB_IMAGE MATHML SRELL FONTCONFIG FRIBIDI HARFBUZZ LIBUNIBREAK UTF8PROC)
    set(USE_${feature} 0)
endforeach()
foreach(feature ZLIB LIBJPEG LIBPNG FREETYPE OWN_MEM_MAN)
    set(USE_${feature} 1)
endforeach()
include(CheckTypeSize)
set(CMAKE_REQUIRED_DEFINITIONS -D_LARGEFILE64_SOURCE=1)
check_type_size(off64_t OFF64_T)
set(CMAKE_REQUIRED_DEFINITIONS)
if(HAVE_OFF64_T)
    set(HAVE_OFF64_T 1)
endif()
configure_file("${CRE_ROOT}/crengine/include/crsetup.h.cmake" "${CMAKE_CURRENT_BINARY_DIR}/crengine-config/crsetup.h" @ONLY)
set(CRE_UNITS chmfmt cp_stats crtxtenc docxfmt drawutil epubfmt fb3fmt hist hyphman
    lstridmap lvdocview lvdrawbuf lvfnt lvfntman lvmemman lvopc lvpagesplitter
    lvstream lvstring lvstsheet lvstyles lvtextfm lvtinydom lvxml mathml mdfmt
    odtfmt odxutil pdbfmt props renderutil rtfimp textlang txtselector wordfmt)
set(CRE_SOURCES "${CRE_ROOT}/crengine/qimagescale/qimagescale.cpp")
foreach(unit IN LISTS CRE_UNITS)
    list(APPEND CRE_SOURCES "${CRE_ROOT}/crengine/src/${unit}.cpp")
endforeach()
# Upstream bakes CSS foreground colors into formatted text. Our reader makes
# theme colors authoritative, using the drawing buffer's current color instead.
# This single headless-adapter hook permits theme redraws without repagination
# and leaves illustrations in their original colors. Keep upstream files intact.
file(READ "${CRE_ROOT}/crengine/src/lvrend.cpp" CRE_RENDER_CODE)
set(CRE_COLOR_HOOK "inline lUInt32 getForegroundColor(const css_style_ref_t style)\n{")
string(FIND "${CRE_RENDER_CODE}" "${CRE_COLOR_HOOK}" CRE_COLOR_HOOK_INDEX)
if(CRE_COLOR_HOOK_INDEX EQUAL -1)
    message(FATAL_ERROR "Pinned CRengine foreground-color hook changed")
endif()
string(REPLACE "${CRE_COLOR_HOOK}" "${CRE_COLOR_HOOK}\n        return LTEXT_COLOR_CURRENT; // EPUB reader: dynamic authoritative theme" CRE_RENDER_CODE "${CRE_RENDER_CODE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/crengine-config/lvrend.cpp" "${CRE_RENDER_CODE}")
list(APPEND CRE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/crengine-config/lvrend.cpp")
include("${PROJECT_SOURCE_DIR}/cmake/CRengineJPEG.cmake")
add_library(crengine STATIC ${CRE_SOURCES})
# epubfmt's bundled SHA1 uses uint8_t without including stdint; the old uClibc
# headers do not provide it transitively as current host headers happen to do.
target_compile_options(crengine PRIVATE -include stdint.h)
target_include_directories(crengine SYSTEM PUBLIC "${CRE_ROOT}/crengine/include" "${CMAKE_CURRENT_BINARY_DIR}/crengine-config")
target_link_libraries(crengine PUBLIC reader_hash PkgConfig::RENDER)
target_compile_definitions(crengine PRIVATE $<$<CONFIG:Debug>:_DEBUG=1> $<$<CONFIG:Debug>:DEBUG=1>)
target_compile_definitions(crengine PUBLIC EPUBREADER_ENGINE_REVISION="ab1981c22e4e67cf9d98be3c301736941d2d271c")
