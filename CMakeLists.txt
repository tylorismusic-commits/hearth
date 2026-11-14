cmake_minimum_required(VERSION 3.16)
project(hearth VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)

enable_testing()

set(LV2_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/lv2"
    CACHE PATH "Directory where LV2 bundles are installed")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(LILV QUIET IMPORTED_TARGET lilv-0)
pkg_check_modules(CAIRO QUIET IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE QUIET IMPORTED_TARGET freetype2)
pkg_check_modules(X11 QUIET IMPORTED_TARGET x11)

add_library(hearth MODULE
  src/hearth.cpp
)
target_link_libraries(hearth PRIVATE PkgConfig::LV2)
set_target_properties(hearth PROPERTIES
  PREFIX ""
  OUTPUT_NAME "hearth"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lv2/hearth.lv2")
set(HEARTH_BUNDLE_FILES
  "${CMAKE_SOURCE_DIR}/lv2/manifest.ttl"
  "${CMAKE_SOURCE_DIR}/lv2/hearth.ttl"
  "${CMAKE_SOURCE_DIR}/lv2/presets.ttl"
  "${CMAKE_SOURCE_DIR}/assets/knob.png"
  "${CMAKE_SOURCE_DIR}/assets/metal.png"
  "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt")

add_custom_target(hearth_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lv2/hearth.lv2"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HEARTH_BUNDLE_FILES}
          "${CMAKE_BINARY_DIR}/lv2/hearth.lv2/"
  DEPENDS ${HEARTH_BUNDLE_FILES})
add_dependencies(hearth_bundle hearth)
target_compile_features(hearth PRIVATE cxx_std_17)
target_include_directories(hearth PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/dsp"
  "${CMAKE_SOURCE_DIR}/src/hearth"
  ${PKG_CONFIG_LV2_INCLUDE_DIRS})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hearth PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)
endif()

set(HEARTH_UI_TARGET "")
if(CAIRO_FOUND AND X11_FOUND AND FREETYPE_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/src/ui/hearth_ui.cpp")
  add_library(hearth_ui MODULE
    src/ui/hearth_ui.cpp
  )
  set(HEARTH_UI_TARGET hearth_ui)
  target_include_directories(hearth_ui PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/dsp"
    "${CMAKE_SOURCE_DIR}/src/hearth")
  target_link_libraries(hearth_ui PRIVATE
    PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2 PkgConfig::FREETYPE)
  set_target_properties(hearth_ui PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hearthUI"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lv2/hearth.lv2")
  target_compile_features(hearth_ui PRIVATE cxx_std_17)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hearth_ui PRIVATE -Wall -Wextra -fvisibility=hidden)
  endif()

  add_executable(hearth_ui_preview EXCLUDE_FROM_ALL
    src/ui/hearth_ui.cpp
  )
  target_compile_definitions(hearth_ui_preview PRIVATE HEARTH_UI_PREVIEW)
  target_include_directories(hearth_ui_preview PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/dsp"
    "${CMAKE_SOURCE_DIR}/src/hearth")
  target_link_libraries(hearth_ui_preview PRIVATE
    PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2 PkgConfig::FREETYPE)

  find_library(XTST_LIBRARY Xtst)
  find_program(XVFB_RUN xvfb-run)
  if(XTST_LIBRARY)
    add_executable(hearth_ui_host_test
      test/ui_host_test.cpp
    )
    target_include_directories(hearth_ui_host_test PRIVATE
      "${CMAKE_SOURCE_DIR}/src/hearth"
      ${PKG_CONFIG_LV2_INCLUDE_DIRS})
    target_link_libraries(hearth_ui_host_test PRIVATE
      PkgConfig::CAIRO PkgConfig::X11 ${CMAKE_DL_LIBS} ${XTST_LIBRARY})
    if(XVFB_RUN)
      add_test(NAME hearth_ui_host
        COMMAND "${XVFB_RUN}" -a -s "-screen 0 1400x1100x24"
          "$<TARGET_FILE:hearth_ui_host_test>"
          "${CMAKE_BINARY_DIR}/lv2/hearth.lv2/hearthUI.so"
          "${CMAKE_BINARY_DIR}/hearth-ui-live.png")
      set_tests_properties(hearth_ui_host PROPERTIES
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
  endif()
endif()

install(TARGETS hearth ${HEARTH_UI_TARGET}
  LIBRARY DESTINATION "${LV2_DIR}/hearth.lv2")
install(FILES
  lv2/manifest.ttl
  lv2/hearth.ttl
  lv2/presets.ttl
  assets/knob.png
  assets/metal.png
  assets/fonts/Jost-ExtraBold.ttf
  assets/fonts/OFL.txt
  DESTINATION "${LV2_DIR}/hearth.lv2")

add_executable(hearth_dsp_test
  test/test_dsp.cpp
)
target_include_directories(hearth_dsp_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/dsp"
  "${CMAKE_SOURCE_DIR}/src/hearth")
target_compile_features(hearth_dsp_test PRIVATE cxx_std_17)
add_test(NAME hearth_dsp COMMAND hearth_dsp_test)

if(LILV_FOUND)
  add_executable(hearth_host_test
    test/host_test.cpp
  )
  target_include_directories(hearth_host_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/dsp"
    "${CMAKE_SOURCE_DIR}/src/hearth")
  target_link_libraries(hearth_host_test PRIVATE PkgConfig::LILV)
  add_test(NAME hearth_host COMMAND hearth_host_test "${CMAKE_BINARY_DIR}/lv2")
endif()
