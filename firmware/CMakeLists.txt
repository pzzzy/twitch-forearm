cmake_minimum_required(VERSION 3.16)
execute_process(COMMAND git -C ${CMAKE_CURRENT_LIST_DIR}/.. rev-parse HEAD
    OUTPUT_VARIABLE FOREARM_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT FOREARM_SOURCE_COMMIT MATCHES "^[0-9a-f]+$")
    set(FOREARM_SOURCE_COMMIT "uncommitted")
endif()
execute_process(COMMAND git -C ${CMAKE_CURRENT_LIST_DIR}/.. status --porcelain
    OUTPUT_VARIABLE FOREARM_SOURCE_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(FOREARM_SOURCE_CHANGES)
    string(APPEND FOREARM_SOURCE_COMMIT "-dirty")
endif()
add_compile_definitions(FOREARM_SOURCE_COMMIT="${FOREARM_SOURCE_COMMIT}")
add_compile_definitions(LV_KCONFIG_IGNORE=1 LV_CONF_PATH=${CMAKE_CURRENT_LIST_DIR}/main/lv_conf.h)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.4.0-beta.1")
project(forearm_chat)

# IDF maps its project directory (firmware), SDK and build roots for reproducible
# DWARF. Shared headers live outside that directory, and compiler headers live
# under a host-specific tool installation. Normalize both before the ELF hash is
# embedded in the application image, without changing runtime __FILE__ strings.
get_filename_component(FOREARM_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../core" REALPATH)
idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=${FOREARM_CORE_ROOT}=/CORE" APPEND)
get_filename_component(FOREARM_COMPILER_PATH "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(FOREARM_COMPILER_BIN "${FOREARM_COMPILER_PATH}" DIRECTORY)
get_filename_component(FOREARM_TOOLCHAIN_ROOT "${FOREARM_COMPILER_BIN}/.." REALPATH)
if(EXISTS "${FOREARM_TOOLCHAIN_ROOT}/lib/gcc" AND
   EXISTS "${FOREARM_TOOLCHAIN_ROOT}/xtensa-esp-elf/include")
    idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=${FOREARM_TOOLCHAIN_ROOT}=/TOOLCHAIN" APPEND)
endif()
