cmake_minimum_required(VERSION 3.24)

project(KatalystTerminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

enable_testing()

# --- C++23 std module cache ---------------------------------------------------
# GCC requires a pre-compiled std.gcm for `import std;`.  Build it once into
# the source tree's gcm.cache/ so every TU can find it.
set(STD_MODULE_SRC "/usr/include/c++/15/bits/std.cc")
set(GCM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gcm.cache")
set(STD_GCM "${GCM_DIR}/std.gcm")

if (NOT EXISTS "${STD_GCM}")
  message(STATUS "Building C++23 std module cache …")
  file(MAKE_DIRECTORY "${GCM_DIR}")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++23 -fmodules
            -c "${STD_MODULE_SRC}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _std_rc
  )
  if (NOT _std_rc EQUAL 0)
    message(FATAL_ERROR "Failed to compile std module (exit ${_std_rc})")
  endif()
endif()

# Enable GCC modules for all TUs — they will `import std;` after the Qt shim.
# Symlink the gcm.cache into the build directory so GCC can find it.
add_compile_options(-fmodules)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fprofile-arcs -ftest-coverage)
  add_link_options(-fprofile-arcs -ftest-coverage)
endif()

# Create a symlink from build/gcm.cache → source/gcm.cache
execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${GCM_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/gcm.cache"
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui DBus Test)
find_package(Vulkan REQUIRED)
find_package(KF6Config REQUIRED)

set(SOURCES
  src/QtShim.h
  src/modules/AnsiColorCore.cpp
  src/modules/SgrParamCore.cpp
  src/MainWindow.cpp
  src/dbus/TerminalDBus.cpp
  src/terminal/PtyProcess.cpp
  src/terminal/TerminalBuffer.cpp
  src/terminal/TerminalConfig.cpp
  src/terminal/TerminalSession.cpp
  src/terminal/VtParserCore.cpp
  src/terminal/VtParser.cpp
  src/ui/TerminalView.cpp
  src/ui/TerminalViewCommon.cpp
  src/ui/TerminalTab.cpp
  src/ui/VulkanTerminalView.cpp
  src/vulkan/VulkanRenderer.cpp
  src/vulkan/VulkanTerminalWindow.cpp
)

add_library(katalyst_lib STATIC ${SOURCES})

target_include_directories(katalyst_lib PUBLIC src)

target_link_libraries(katalyst_lib Qt6::Widgets Qt6::Gui Qt6::DBus Vulkan::Vulkan KF6::ConfigCore)

add_executable(katalyst-terminal src/main.cpp)

target_link_libraries(katalyst-terminal katalyst_lib)

set_source_files_properties(src/terminal/VtParserCore.cpp PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/katalyst_lib.dir/src/modules/AnsiColorCore.cpp.o;${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/katalyst_lib.dir/src/modules/SgrParamCore.cpp.o"
)

set(SHADER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terminal_quad.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terminal_quad.frag
)

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
find_program(GLSLANG_VALIDATOR glslangValidator)

if (GLSLANG_VALIDATOR)
  foreach(SHADER_FILE IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
      OUTPUT ${SHADER_SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
      COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_FILE} -o ${SHADER_SPV}
      DEPENDS ${SHADER_FILE}
      COMMENT "Compiling ${SHADER_NAME}"
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_SPV})
  endforeach()

  add_custom_target(katalyst-terminal-shaders ALL DEPENDS ${SHADER_OUTPUTS})
  add_dependencies(katalyst-terminal katalyst-terminal-shaders)
else()
  message(WARNING "glslangValidator not found; Vulkan shaders will not be built.")
endif()

add_executable(test_terminal tests/test_terminal.cpp)
target_link_libraries(test_terminal katalyst_lib Qt6::Test)
add_test(NAME test_terminal COMMAND test_terminal)

find_program(GCOVR gcovr)

if(GCOVR)

  add_custom_target(coverage

    COMMAND ${GCOVR} -r ${CMAKE_CURRENT_SOURCE_DIR} --html --html-details -o ${CMAKE_CURRENT_SOURCE_DIR}/coverage/coverage.html ${CMAKE_CURRENT_BINARY_DIR}

    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}

    DEPENDS test_terminal

  )

endif()

target_link_libraries(katalyst-terminal
  katalyst_lib
)

target_precompile_headers(katalyst_lib PUBLIC src/QtShim.h)

# C++23 modules must have "export module ..." as their absolute first declaration.
# The precompiled header injected by CMake via "-include cmake_pch.hxx" breaks this rule.
# Disable PCH for our raw module interface files.
set_source_files_properties(
  src/modules/AnsiColorCore.cpp
  src/modules/SgrParamCore.cpp
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(katalyst-terminal PRIVATE src)

install(TARGETS katalyst-terminal RUNTIME DESTINATION bin)
install(FILES data/org.katalyst.Terminal.desktop DESTINATION share/applications)
if (SHADER_OUTPUTS)
  install(FILES ${SHADER_OUTPUTS} DESTINATION share/katalyst-terminal/shaders)
endif()
