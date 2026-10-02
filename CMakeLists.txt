cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(so_loader C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Army of Darkness Defense")
set(VITA_TITLEID "AODD00001")
set(VITA_VPKNAME "aod_vita")
set(VITA_VERSION "01.10")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12 -s NP_COMMUNICATION_ID=AODD00001")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/aodd/") # Path to data (with trailing /)
set(SO_PATH "${DATA_PATH}libgame.so") # Path to .so
set(FMOD_SO_PATH "${DATA_PATH}libfmodex.so")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}" -DFMOD_SO_PATH="${FMOD_SO_PATH}")

# Port type
option(NDK_PORT "This should be on if the game is done with Android NDK (has an android_main entry point)" OFF)
if (NDK_PORT)
  add_definitions(-DNDK_PORT)
endif()

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

set(SHADER_FORMAT "GLSL") # Preferred shader format (one of 'GLSL', 'CG', 'GXP')
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" OFF)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Unique debug definition for loader's logging to not interfere with anything
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_SOLOADER)
  # Uncomment this for verbose FalsoJNI logging:
  #add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
  # Uncomment this for OpenGL logging:
  #add_definitions(-DDEBUG_OPENGL)
  # Uncomment this for so_util logging:
  #add_definitions(-DSO_UTIL_VERBOSE=1)
  # Uncomment this for verbose FalsoNDK logging (separate things):
  #add_definitions(-DDEBUG_POLL_AND_WAKE=1)
  #add_definitions(-DDEBUG_EPOLL=1)
  #add_definitions(-DDEBUG_PIPEFD=1)
  #add_definitions(-DDEBUG_CALLBACKS=1)
  # Space-separated flags passed to vitaGL
  set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 NO_SPLASHSCREEN=1 SAFE_UNIFORMS=1 TEST=1 LOG_ERRORS=1")
else()
  # Space-separated flags passed to vitaGL
  # NO_SPLASHSCREEN=1: vitaGL's boot splash ignores vglSwapBuffers until the first draw, which
  # hid fatal_error() dialogs behind a permanent spinning logo (FIRST-BOOT-TRIAGE.md).
  # SAFE_UNIFORMS=1: packed uniform locations with an element offset, used by the dense
  # uniform-id layer (source/aod/uniform_remap.c) to address array elements (boot-04 crash).
  set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 NO_SPLASHSCREEN=1 SAFE_UNIFORMS=1 NO_DEBUG=1")
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")
# Release builds must not embed the builder's directory: FalsoJNI's log macros use __FILE__.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=/usr/src/army-of-darkness-defense-vita")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=/usr/src/army-of-darkness-defense-vita")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif()

set(SOURCES
    source/main.c
    source/dynlib.c
    source/java.c
    source/aod/trophies.c
    source/aod/trophy_storage.c
    source/aod/trophies_vita.c
    source/aod/hash.c
    source/aod/jni_bridge.c
    source/aod/port_vita.c
    source/aod/text_render.c
    source/aod/touch_ids.c
    source/aod/uniform_remap.c
    source/aod/uniform_remap_vita.c
    source/aod/gl_diag.c
    source/aod/gl_diag_vita.c
    source/aod/viewport_softfp.c
    source/aod/audio_diag.c
    source/aod/opensl_compat.c
    source/aod/fmod_diag.c
    source/aod/fmod_diag_vita.c
    source/aod/audio_diag_vita.c
    source/aod/gxm_diag.c
    source/aod/gxm_diag_vita.c
    source/aod/newlib_glue.c
    source/patch.c
    source/reimpl/bits/_ctype.c
    source/reimpl/errno.c
    source/reimpl/io.c
    source/reimpl/log.c
    source/reimpl/mem.c
    source/reimpl/pthr.c
    source/reimpl/sys.c
    source/reimpl/time64.c
    source/utils/dialog.c
    source/utils/glutil.c
    source/utils/init.c
    source/utils/logger.c
    source/utils/settings.c
    source/utils/utils.c
    lib/falso_jni/FalsoJNI.c
    lib/falso_jni/FalsoJNI_ImplBridge.c
    lib/falso_jni/FalsoJNI_Logger.c
    lib/falso_jni/converter.c
    lib/sha1/sha1.c
    lib/fios/fios.c
    lib/so_util/so_util.c)

if(NOT NDK_PORT)
list(APPEND SOURCES
  source/reimpl/asset_manager.cpp
  source/reimpl/controls.c
  source/reimpl/controls.h)
endif()

set_source_files_properties(source/aod/trophies.c PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

if(NDK_PORT)
  add_subdirectory(lib/falso_ndk)
  add_dependencies(${CMAKE_PROJECT_NAME} FalsoNDK)
endif()

# VitaGL built from lib/vitagl via its own Makefile.
# Made this way so that testing quick adjustments to VitaGL is possible,
# as well as changing build flags in a fast and efficient way.
separate_arguments(VITAGL_MAKE_ARGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS}")

set(VITAGL_DIR ${CMAKE_SOURCE_DIR}/lib/vitagl)
set(VITAGL_LIB ${VITAGL_DIR}/libvitaGL.a)

# Force a full rebuild of vitaGL whenever VITAGL_MAKE_FLAGS change
set(VITAGL_FLAGS_STAMP ${CMAKE_BINARY_DIR}/vitagl_flags.stamp)
set(vitagl_old_flags "")
if (EXISTS ${VITAGL_FLAGS_STAMP})
  file(READ ${VITAGL_FLAGS_STAMP} vitagl_old_flags)
endif()
if (NOT vitagl_old_flags STREQUAL VITAGL_MAKE_FLAGS)
  file(GLOB_RECURSE vitagl_stale_objs ${VITAGL_DIR}/source/*.o)
  file(REMOVE ${vitagl_stale_objs} ${VITAGL_LIB})
  file(WRITE ${VITAGL_FLAGS_STAMP} "${VITAGL_MAKE_FLAGS}")
endif()

include(ProcessorCount)
ProcessorCount(VITAGL_NPROC)
if (VITAGL_NPROC EQUAL 0)
  set(VITAGL_NPROC 4)
endif()

add_custom_target(vitaGL_build
                  COMMAND ${CMAKE_COMMAND} -E env CPLUS_INCLUDE_PATH=${VITAGL_DIR}/source C_INCLUDE_PATH=${VITAGL_DIR}/source make -C ${VITAGL_DIR} -j4 ${VITAGL_MAKE_ARGS}
                  BYPRODUCTS ${VITAGL_LIB}
                  COMMENT "Building VitaGL"
                  VERBATIM)

add_library(vitaGL STATIC IMPORTED)
set_target_properties(vitaGL PROPERTIES
                      IMPORTED_LOCATION ${VITAGL_LIB}
                      INTERFACE_INCLUDE_DIRECTORIES ${VITAGL_DIR}/source)
add_dependencies(${CMAKE_PROJECT_NAME} vitaGL_build)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

if(NDK_PORT)
  target_link_libraries(${CMAKE_PROJECT_NAME}
                        FalsoNDK
                        )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE $ENV{VITASDK}/arm-vita-eabi/include/freetype2)

# vita-elf-create appends SCE metadata (~5 KiB here) to the end of the text segment, and the SDK
# linker script starts the data segment at the next 64 KiB boundary, so the free gap depends on
# the text size and can fall below what is needed (it did at 36d8186+E8: 2684 < 4968 bytes). The
# SDK script reserves __sce_headroom bytes before that alignment when the symbol is defined.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--defsym,__sce_headroom=0x4000)
# FIX (boot-08): vitaGL's HAVE_SOFTFP_ABI viewport shim converted the float arguments a second
# time on top of the softfp SDK's own sceGxmSetViewport adapter; see source/aod/viewport_softfp.c.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--wrap=sceGxmSetViewport_sfp)
# Audio evidence (boot-10): count/peak the SDK OpenSL ES library's Vita port traffic; see audio_diag.h.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,--wrap=sceAudioOutOpenPort -Wl,--wrap=sceAudioOutOutput -Wl,--wrap=sceAudioOutSetVolume
    -Wl,--wrap=IOutputMixExt_FillBuffer)
# DIAGNOSTIC E9 (boot-07): interpose on vitaGL's sceGxm* calls; the shims in
# source/aod/gxm_diag_vita.c pass arguments and return codes through unchanged.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,--wrap=sceGxmBeginScene -Wl,--wrap=sceGxmEndScene -Wl,--wrap=sceGxmDraw -Wl,--wrap=sceGxmDrawInstanced
    -Wl,--wrap=sceGxmCreateRenderTarget -Wl,--wrap=sceGxmColorSurfaceInit -Wl,--wrap=sceGxmDepthStencilSurfaceInit
    -Wl,--wrap=sceGxmDisplayQueueAddEntry -Wl,--wrap=sceGxmShaderPatcherCreateVertexProgram
    -Wl,--wrap=sceGxmShaderPatcherCreateFragmentProgram)

target_link_libraries(${CMAKE_PROJECT_NAME}
                      freetype
                      png
                      bz2
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      OpenSLES
                      sndfile
                      mp3lame
                      mpg123
                      opus
                      vorbis
                      ogg
                      vorbisenc
                      vorbisfile
                      FLAC
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      SceNpTrophy_stub
                      SceSysmodule_stub
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
# Clean original metadata/art; generated locally, not publisher assets.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(AOD_TROPHY_PACK "${CMAKE_BINARY_DIR}/trophy/AODD00001_00/TROPHY.TRP")
add_custom_command(OUTPUT ${AOD_TROPHY_PACK}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/release/trophies/build_pack.py --out ${AOD_TROPHY_PACK}
    DEPENDS release/trophies/build_pack.py release/trophies/mapping.json release/trophies/metadata.json
    VERBATIM)
add_custom_target(aod_trophy_pack DEPENDS ${AOD_TROPHY_PACK})
add_dependencies(${CMAKE_PROJECT_NAME} aod_trophy_pack)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${AOD_TROPHY_PACK} sce_sys/trophy/AODD00001_00/TROPHY.TRP
                ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/urandom urandom
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
