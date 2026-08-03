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
# Detect the GCC major version so we don't hardcode the include path.
execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
  OUTPUT_VARIABLE _gcc_full_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE "([0-9]+).*" "\\1" _gcc_major "${_gcc_full_version}")
find_file(STD_MODULE_SRC "bits/std.cc"
  PATHS "/usr/include/c++/${_gcc_major}"
  NO_DEFAULT_PATH
  REQUIRED
)
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
file(CREATE_LINK "${GCM_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/gcm.cache" SYMBOLIC)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui DBus Test)
find_package(KF6Config REQUIRED)

# C++23 module interface files must be compiled before any TU that imports them.
# Build them as a separate OBJECT library so we can enforce ordering portably.
add_library(katalyst_modules OBJECT
  src/modules/AnsiColorCore.cpp
  src/modules/SgrParamCore.cpp
)
target_include_directories(katalyst_modules PUBLIC src)
target_link_libraries(katalyst_modules Qt6::Widgets Qt6::Gui)

set(SOURCES
  src/QtShim.h
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
)

add_library(katalyst_lib STATIC ${SOURCES} $<TARGET_OBJECTS:katalyst_modules>)
add_dependencies(katalyst_lib katalyst_modules)

target_include_directories(katalyst_lib PUBLIC src)

target_link_libraries(katalyst_lib
  Qt6::Widgets
  Qt6::Gui
  Qt6::DBus
  KF6::ConfigCore
  KF6::ConfigGui
)

add_executable(katalyst-terminal src/main.cpp)

target_link_libraries(katalyst-terminal katalyst_lib)

add_executable(test_terminal tests/test_terminal.cpp)
target_link_libraries(test_terminal katalyst_lib Qt6::Test)
add_test(NAME test_terminal COMMAND test_terminal)

add_executable(test_ui tests/test_ui.cpp)
target_link_libraries(test_ui katalyst_lib Qt6::Test Qt6::Widgets)
add_test(NAME test_ui COMMAND test_ui)

find_program(GCOVR gcovr)

if(GCOVR)

  add_custom_target(coverage

    COMMAND ${GCOVR} -r ${CMAKE_CURRENT_SOURCE_DIR} --html --html-details -o ${CMAKE_CURRENT_SOURCE_DIR}/coverage/coverage.html ${CMAKE_CURRENT_BINARY_DIR}

    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}

    DEPENDS test_terminal

  )

endif()

target_precompile_headers(katalyst_lib PUBLIC src/QtShim.h)

target_include_directories(katalyst-terminal PRIVATE src)

install(TARGETS katalyst-terminal RUNTIME DESTINATION bin)
install(FILES data/org.katalyst.Terminal.desktop DESTINATION share/applications)
