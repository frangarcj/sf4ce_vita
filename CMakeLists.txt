cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(SF4CE.elf C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "Street Fighter IV CE")
set(VITA_TITLEID  "SF4CE0000")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q,--wrap,memcpy,--wrap,memmove,--wrap,memset -Wall -O3 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++17")

set(VPK_FILE_ARGS
  ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/config.png sce_sys/livearea/contents/config.png
  ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

set(CMAKE_SOURCE_DIR ${CMAKE_SOURCE_DIR})
add_executable(SF4CE
  loader/main.cpp
  loader/bionic_compat.cpp
  loader/bridge.c
  loader/so_util.c
)

target_include_directories(SF4CE PRIVATE
  ${VITASDK}/arm-vita-eabi/include/freetype2
)

target_link_libraries(SF4CE
  vitaGL
  vitaShaRK
  kubridge_stub
  mathneon
  OpenSLES
  SceCtrl_stub
  SceGxm_stub
  SceAppUtil_stub
  SceAppMgr_stub
  SceAudio_stub
  SceAudioIn_stub
  SceTouch_stub
  SceCommonDialog_stub
  SceIme_stub
  SceDisplay_stub
  ScePower_stub
  SceSysmodule_stub
  SceKernelDmacMgr_stub
  SceShaccCg_stub
  SceShaccCgExt
  taihen_stub
  pthread
  freetype
  bz2
  png
  z
  m
  c
)

vita_create_self(eboot.bin SF4CE UNSAFE)

vita_create_vpk(SF4CE.vpk "${VITA_TITLEID}" eboot.bin
  VERSION "${VITA_VERSION}"
  NAME "${VITA_APP_NAME}"
  FILE ${VPK_FILE_ARGS}
)
