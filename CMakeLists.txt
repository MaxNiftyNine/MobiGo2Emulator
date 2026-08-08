cmake_minimum_required(VERSION 3.20)
project(mobigo2_unsp_emu LANGUAGES CXX)
include(CheckIPOSupported)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MOBIGO2_NATIVE_OPTIMIZATIONS "Build with host-native CPU optimizations" OFF)
option(MOBIGO2_PGO_GENERATE "Instrument the emulator to generate a Clang PGO profile" OFF)
set(MOBIGO2_PGO_PROFILE "" CACHE FILEPATH "Clang .profdata file used to optimize the emulator")

if(MOBIGO2_PGO_GENERATE AND MOBIGO2_PGO_PROFILE)
  message(FATAL_ERROR "Select either MOBIGO2_PGO_GENERATE or MOBIGO2_PGO_PROFILE, not both")
endif()
if((MOBIGO2_PGO_GENERATE OR MOBIGO2_PGO_PROFILE) AND
   NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The built-in PGO workflow currently requires Clang or AppleClang")
endif()

if(EMSCRIPTEN)
  set(MOBIGO2_NATIVE_OPTIMIZATIONS OFF CACHE BOOL "" FORCE)
  set(SDL2_INCLUDE_DIRS "")
  set(SDL2_LIBRARY_DIRS "")
  set(SDL2_LIBRARIES "")
  set(SDL2_CFLAGS_OTHER "")
else()
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    set(SDL2_LIBRARIES SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
      set(MOBIGO2_SDL_MAIN_TARGET SDL2::SDL2main)
    endif()
    set(SDL2_INCLUDE_DIRS "")
    set(SDL2_LIBRARY_DIRS "")
    set(SDL2_CFLAGS_OTHER "")
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
  endif()
endif()

if(MSVC)
  set(MOBIGO2_WARNING_FLAGS /W4)
else()
  set(MOBIGO2_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

add_executable(mobigo2_emu
  src/main.cpp
  src/accelerometer.hpp
  src/audio.hpp
  src/boot.hpp
  src/bus.hpp
  src/common.hpp
  src/cpu.hpp
  src/devices.hpp
  src/mba_overlay.hpp
  src/realtime_throttle.hpp
  src/usb_panel.hpp
  src/video.hpp
)
if(WIN32)
  # Match the distributed Windows launcher: SDL owns WinMain and launching the
  # emulator does not create a second console window.
  set_property(TARGET mobigo2_emu PROPERTY WIN32_EXECUTABLE TRUE)
endif()
target_include_directories(mobigo2_emu PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_directories(mobigo2_emu PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(mobigo2_emu PRIVATE ${MOBIGO2_SDL_MAIN_TARGET} ${SDL2_LIBRARIES})
target_compile_options(mobigo2_emu PRIVATE ${SDL2_CFLAGS_OTHER} ${MOBIGO2_WARNING_FLAGS})
if(MOBIGO2_PGO_GENERATE)
  target_compile_options(mobigo2_emu PRIVATE -fprofile-instr-generate)
  target_link_options(mobigo2_emu PRIVATE -fprofile-instr-generate)
elseif(MOBIGO2_PGO_PROFILE)
  if(NOT EXISTS "${MOBIGO2_PGO_PROFILE}")
    message(FATAL_ERROR "PGO profile does not exist: ${MOBIGO2_PGO_PROFILE}")
  endif()
  target_compile_options(mobigo2_emu PRIVATE -fprofile-instr-use=${MOBIGO2_PGO_PROFILE})
  target_link_options(mobigo2_emu PRIVATE -fprofile-instr-use=${MOBIGO2_PGO_PROFILE})
endif()
if(EMSCRIPTEN)
  target_compile_options(mobigo2_emu PRIVATE -sUSE_SDL=2)
  target_link_options(mobigo2_emu PRIVATE
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=134217728
    -sASSERTIONS=1
    -sNO_EXIT_RUNTIME=1
    -sFORCE_FILESYSTEM=1
    --preload-file=${CMAKE_SOURCE_DIR}/firmware/internalrom.bin@/internalrom.bin
    --preload-file=${CMAKE_SOURCE_DIR}/firmware/spi.bin@/spi.bin
    --preload-file=${CMAKE_SOURCE_DIR}/firmware/nand.us-stitched.bin.part00@/nand.bin.part00
  )
endif()
if(MOBIGO2_NATIVE_OPTIMIZATIONS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(mobigo2_emu PRIVATE $<$<CONFIG:Release>:-march=native>)
endif()

check_ipo_supported(RESULT MOBIGO2_IPO_SUPPORTED OUTPUT MOBIGO2_IPO_ERROR)
if(MOBIGO2_IPO_SUPPORTED)
  set_property(TARGET mobigo2_emu PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(BUILD_TESTING)
  set(MOBIGO2_SDL_TEST_LIBRARIES ${SDL2_LIBRARIES})
  set(MOBIGO2_SDL_TEST_CFLAGS ${SDL2_CFLAGS_OTHER})
  if(WIN32)
    # MSYS2's sdl2.pc includes the GUI-entry shim. Unit tests use a normal
    # console main() and must link SDL itself without SDL2main/mingw32.
    list(REMOVE_ITEM MOBIGO2_SDL_TEST_LIBRARIES SDL2main mingw32)
    list(REMOVE_ITEM MOBIGO2_SDL_TEST_CFLAGS "-Dmain=SDL_main")
  endif()

  add_executable(audio_test tests/audio_test.cpp)
  target_compile_definitions(audio_test PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(audio_test PRIVATE src ${SDL2_INCLUDE_DIRS})
  target_link_directories(audio_test PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(audio_test PRIVATE ${MOBIGO2_SDL_TEST_LIBRARIES})
  target_compile_options(audio_test PRIVATE ${MOBIGO2_SDL_TEST_CFLAGS} ${MOBIGO2_WARNING_FLAGS})
  add_test(NAME audio_test COMMAND audio_test)

  add_executable(watchdog_test tests/watchdog_test.cpp)
  target_compile_definitions(watchdog_test PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(watchdog_test PRIVATE src ${SDL2_INCLUDE_DIRS})
  target_link_directories(watchdog_test PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(watchdog_test PRIVATE ${MOBIGO2_SDL_TEST_LIBRARIES})
  target_compile_options(watchdog_test PRIVATE ${MOBIGO2_SDL_TEST_CFLAGS} ${MOBIGO2_WARNING_FLAGS})
  add_test(NAME watchdog_test COMMAND watchdog_test)

  add_executable(hardware_accuracy_test tests/hardware_accuracy_test.cpp)
  target_compile_definitions(hardware_accuracy_test PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(hardware_accuracy_test PRIVATE src ${SDL2_INCLUDE_DIRS})
  target_link_directories(hardware_accuracy_test PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(hardware_accuracy_test PRIVATE ${MOBIGO2_SDL_TEST_LIBRARIES})
  target_compile_options(hardware_accuracy_test PRIVATE ${MOBIGO2_SDL_TEST_CFLAGS} ${MOBIGO2_WARNING_FLAGS})
  add_test(NAME hardware_accuracy_test COMMAND hardware_accuracy_test)

  add_executable(configuration_test tests/configuration_test.cpp)
  target_compile_definitions(configuration_test PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(configuration_test PRIVATE src ${SDL2_INCLUDE_DIRS})
  target_link_directories(configuration_test PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(configuration_test PRIVATE ${MOBIGO2_SDL_TEST_LIBRARIES})
  target_compile_options(configuration_test PRIVATE ${MOBIGO2_SDL_TEST_CFLAGS} ${MOBIGO2_WARNING_FLAGS})
  add_test(NAME configuration_test COMMAND configuration_test)
endif()
