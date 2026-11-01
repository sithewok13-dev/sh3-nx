cmake_minimum_required(VERSION 3.20)
project(sh3nx C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB GEN CONFIGURE_DEPENDS src/recomp/gen/*.c)
file(GLOB RT CONFIGURE_DEPENDS src/runtime/*.c src/win32/*.c src/d3d8/*.c src/game/*.c)

add_executable(sh3 ${GEN} ${RT})
target_include_directories(sh3 PRIVATE src/recomp/gen)
# Lifted code relies on wrapping arithmetic and type punning through guest memory.
target_compile_options(sh3 PRIVATE -fwrapv -fno-strict-aliasing)
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-w")
# The launcher's images are built into the binary (src/d3d8/launcher.c).
file(SHA1 ${CMAKE_SOURCE_DIR}/assets/splash.bmp SPLASH_SHA1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/splash.bmp)
set_source_files_properties(src/d3d8/launcher.c PROPERTIES COMPILE_DEFINITIONS
                            "SRC_DIR=\"${CMAKE_SOURCE_DIR}\";SPLASH_SHA1=\"${SPLASH_SHA1}\""
                            OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/splash.bmp;${CMAKE_SOURCE_DIR}/assets/logo.bmp;${CMAKE_SOURCE_DIR}/assets/font.bmp")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  # An iPhone/iPad app (tools/ios/build.sh): OpenGL ES 3.0 and a static SDL2 from tools/ios/build-deps.sh.
  # The bundle is unsigned: SideStore, AltStore or Sideloadly sign it on install.
  set(SH2_IOS_DEPS "${CMAKE_SOURCE_DIR}/build/ios-deps" CACHE PATH "SDL2 built for iOS (tools/ios/build-deps.sh)")
  target_compile_definitions(sh3 PRIVATE SH2_IOS SH2_GLES)
  target_include_directories(sh3 PRIVATE ${SH2_IOS_DEPS}/include)
  target_link_libraries(sh3 PRIVATE ${SH2_IOS_DEPS}/lib/libSDL2main.a ${SH2_IOS_DEPS}/lib/libSDL2.a iconv m
    "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreBluetooth"
    "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreHaptics" "-framework CoreMotion"
    "-framework Foundation" "-framework GameController" "-framework Metal" "-framework OpenGLES"
    "-framework QuartzCore" "-framework UIKit")
  set(SH2_IOS_BUNDLE_ID "com.example.sh3-nx" CACHE STRING "The app's bundle identifier")
  set(SH2_IOS_VERSION "0.06")
  set(SH2_IOS_BUILD "1" CACHE STRING "The build number (CFBundleVersion): the cloud build passes its run number")
  target_compile_definitions(sh3 PRIVATE SH2_VERSION_LABEL="${SH2_IOS_VERSION} (${SH2_IOS_BUILD})")
  configure_file(ios/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)
  set_target_properties(sh3 PROPERTIES
    OUTPUT_NAME SH3
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist)
elseif(NINTENDO_SWITCH)
  # Built with the devkitPro toolchain (tools/build-switch.sh); OpenGL comes through EGL and mesa.
  target_include_directories(sh3 PRIVATE ${DEVKITPRO}/portlibs/switch/include)
  target_link_libraries(sh3 PRIVATE SDL2 EGL glapi drm_nouveau stdc++ pthread m)
  nx_generate_nacp(sh3.nacp NAME "Silent Hill 3" AUTHOR "sh3-nx" VERSION "0.06")
  nx_create_nro(sh3 OUTPUT sh3-nx.nro NACP sh3.nacp NOICON)
else()
  # Desktop GL is loaded at run time through SDL (src/d3d8/gl.c), so nothing links libGL.
  # SH2_GLES draws with OpenGL ES 3.0 instead, linked directly: what iOS has, and testable on Linux through mesa.
  option(SH2_GLES "Draw with OpenGL ES 3.0 (the iOS renderer)" OFF)
  find_package(SDL2 REQUIRED)
  target_link_libraries(sh3 PRIVATE SDL2::SDL2 m pthread)
  if(SH2_GLES)
    target_compile_definitions(sh3 PRIVATE SH2_GLES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
    target_link_libraries(sh3 PRIVATE PkgConfig::GLES)
  endif()
endif()
