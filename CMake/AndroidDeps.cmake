# Dependencias nativas para Android.
# En escritorio vienen de vcpkg / apt; en Android no existe ninguno de los dos, asi que se descargan
# y se compilan aqui como librerias estaticas. Se incluye desde el CMakeLists raiz solo si ANDROID.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Todo estatico y sin pruebas solo para estas librerias; BUILD_SHARED_LIBS se restaura al final.
set(_anchor_saved_bsl "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

# ------------------------------------------------------------------ libpng (lo pide ZAPD)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(PNG_ARM_NEON off CACHE STRING "" FORCE)
FetchContent_Declare(libpng
    URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz)
FetchContent_MakeAvailable(libpng)
if(TARGET png_static AND NOT TARGET PNG::PNG)
    add_library(PNG::PNG ALIAS png_static)
endif()
set(PNG_FOUND TRUE)
set(PNG_INCLUDE_DIRS "${libpng_SOURCE_DIR};${libpng_BINARY_DIR}")
set(PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}")
set(PNG_PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}")
set(PNG_LIBRARIES png_static)
set(PNG_LIBRARY png_static)

# ------------------------------------------------------------------ ogg
FetchContent_Declare(ogg
    URL https://github.com/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.tar.gz)
FetchContent_MakeAvailable(ogg)
if(TARGET ogg AND NOT TARGET Ogg::ogg)
    add_library(Ogg::ogg ALIAS ogg)
endif()
set(Ogg_FOUND TRUE)

# Desde aqui find_package de estos paquetes se da por resuelto (los targets existen en este build).
# Se conserva el comportamiento normal para todo lo demas mediante _find_package.
set(ANDROID_PROVIDED_PACKAGES PNG Ogg Vorbis Opus OpusFile)
macro(find_package)
    if("${ARGV0}" IN_LIST ANDROID_PROVIDED_PACKAGES)
        set(${ARGV0}_FOUND TRUE)
    else()
        _find_package(${ARGV})
    endif()
endmacro()

# ------------------------------------------------------------------ vorbis
FetchContent_Declare(vorbis
    URL https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz)
FetchContent_MakeAvailable(vorbis)
foreach(_v vorbis vorbisfile vorbisenc)
    if(TARGET ${_v} AND NOT TARGET Vorbis::${_v})
        add_library(Vorbis::${_v} ALIAS ${_v})
    endif()
endforeach()

# ------------------------------------------------------------------ opus (CMake propio) y opusfile (sin CMake)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
FetchContent_Declare(opus
    URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz)
FetchContent_MakeAvailable(opus)
if(TARGET opus AND NOT TARGET Opus::opus)
    add_library(Opus::opus ALIAS opus)
endif()

FetchContent_Declare(opusfile
    URL https://github.com/xiph/opusfile/releases/download/v0.12/opusfile-0.12.tar.gz)
FetchContent_GetProperties(opusfile)
if(NOT opusfile_POPULATED)
    FetchContent_Populate(opusfile)
endif()

# El juego incluye <opus/opus.h> y <opusfile.h>; opus trae sus headers planos, asi que se arma un
# directorio con la estructura que espera el codigo.
set(ANDROID_AUDIO_INC "${CMAKE_BINARY_DIR}/android_audio_inc")
file(MAKE_DIRECTORY "${ANDROID_AUDIO_INC}/opus")
file(GLOB _opus_headers "${opus_SOURCE_DIR}/include/*.h")
file(COPY ${_opus_headers} DESTINATION "${ANDROID_AUDIO_INC}/opus")
file(COPY "${opusfile_SOURCE_DIR}/include/opusfile.h" DESTINATION "${ANDROID_AUDIO_INC}")

add_library(opusfile STATIC
    ${opusfile_SOURCE_DIR}/src/info.c
    ${opusfile_SOURCE_DIR}/src/internal.c
    ${opusfile_SOURCE_DIR}/src/opusfile.c
    ${opusfile_SOURCE_DIR}/src/stream.c)
target_include_directories(opusfile
    PUBLIC "${ANDROID_AUDIO_INC}" "${ANDROID_AUDIO_INC}/opus"
    PRIVATE "${opusfile_SOURCE_DIR}/src")
target_compile_definitions(opusfile PRIVATE OP_DISABLE_HTTP OP_DISABLE_DOCS OP_DISABLE_EXAMPLES)
target_link_libraries(opusfile PUBLIC opus ogg)
add_library(OpusFile::opusfile ALIAS opusfile)

foreach(_t vorbis vorbisfile vorbisenc opus)
    if(TARGET ${_t})
        target_include_directories(${_t} INTERFACE "${ANDROID_AUDIO_INC}")
    endif()
endforeach()

set(BUILD_SHARED_LIBS "${_anchor_saved_bsl}")
