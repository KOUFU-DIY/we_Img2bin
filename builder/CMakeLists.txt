cmake_minimum_required(VERSION 3.20)

project(img2bin_tools VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

enable_testing()

set(IMG2BIN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(IMG2BIN_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/icon")
set(IMG2BIN_WINDOWS_RC_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/windows_exe.rc.in")
get_filename_component(IMG2BIN_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(TO_CMAKE_PATH "${IMG2BIN_REPO_ROOT}" IMG2BIN_SOURCE_DIR_CMAKE)

function(img2bin_add_windows_resources target_name)
  if(NOT WIN32)
    return()
  endif()

  set(options)
  set(oneValueArgs FILE_DESCRIPTION INTERNAL_NAME ORIGINAL_FILENAME PRODUCT_NAME ICON)
  cmake_parse_arguments(IMG2BIN_WINRES "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT IMG2BIN_WINRES_FILE_DESCRIPTION)
    set(IMG2BIN_WINRES_FILE_DESCRIPTION "${target_name}")
  endif()
  if(NOT IMG2BIN_WINRES_INTERNAL_NAME)
    set(IMG2BIN_WINRES_INTERNAL_NAME "${target_name}")
  endif()
  if(NOT IMG2BIN_WINRES_ORIGINAL_FILENAME)
    set(IMG2BIN_WINRES_ORIGINAL_FILENAME "${target_name}.exe")
  endif()
  if(NOT IMG2BIN_WINRES_PRODUCT_NAME)
    set(IMG2BIN_WINRES_PRODUCT_NAME "${target_name}")
  endif()

  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h" IMG2BIN_RC_VERSION_HEADER_PATH)

  set(IMG2BIN_RC_FILE_DESCRIPTION "${IMG2BIN_WINRES_FILE_DESCRIPTION}")
  set(IMG2BIN_RC_INTERNAL_NAME "${IMG2BIN_WINRES_INTERNAL_NAME}")
  set(IMG2BIN_RC_ORIGINAL_FILENAME "${IMG2BIN_WINRES_ORIGINAL_FILENAME}")
  set(IMG2BIN_RC_PRODUCT_NAME "${IMG2BIN_WINRES_PRODUCT_NAME}")
  set(IMG2BIN_RC_ICON_DECLARATION "")

  if(DEFINED IMG2BIN_WINRES_ICON AND NOT "${IMG2BIN_WINRES_ICON}" STREQUAL "")
    if(EXISTS "${IMG2BIN_WINRES_ICON}")
      file(TO_CMAKE_PATH "${IMG2BIN_WINRES_ICON}" IMG2BIN_RC_ICON_PATH)
      set(IMG2BIN_RC_ICON_DECLARATION "1 ICON \"${IMG2BIN_RC_ICON_PATH}\"")
    else()
      message(WARNING "Windows icon file for target ${target_name} was not found: ${IMG2BIN_WINRES_ICON}. Icon embedding will be skipped.")
    endif()
  endif()

  set(IMG2BIN_RC_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.rc")
  configure_file("${IMG2BIN_WINDOWS_RC_TEMPLATE}" "${IMG2BIN_RC_OUTPUT}" @ONLY)
  target_sources(${target_name} PRIVATE "${IMG2BIN_RC_OUTPUT}")
endfunction()

add_library(img2bin_core STATIC
  src/core/cli.c
  src/core/filesystem.c
  src/core/format.c
  src/core/image_io.c
  src/core/util.c
)
target_include_directories(img2bin_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${IMG2BIN_THIRD_PARTY_DIR}"
)

add_library(img2bin_tool_app_core STATIC
  src/apps/tool_app.c
)
target_include_directories(img2bin_tool_app_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${IMG2BIN_THIRD_PARTY_DIR}"
)
target_link_libraries(img2bin_tool_app_core PUBLIC img2bin_core)

add_library(img2bin_raw_algo STATIC
  src/algorithms/raw/raw_encoder.c
)
target_include_directories(img2bin_raw_algo
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/raw"
)
target_link_libraries(img2bin_raw_algo PUBLIC img2bin_core)

add_library(img2bin_imprle_algo STATIC
  src/algorithms/imprle/imprle_encoder.c
)
target_include_directories(img2bin_imprle_algo
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/raw"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/imprle"
)
target_link_libraries(img2bin_imprle_algo PUBLIC img2bin_core img2bin_raw_algo)

add_library(img2bin_rle_algo STATIC
  src/algorithms/rle/rle_encoder.c
)
target_include_directories(img2bin_rle_algo
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/raw"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/rle"
)
target_link_libraries(img2bin_rle_algo PUBLIC img2bin_core img2bin_raw_algo)

add_library(img2bin_qoi_algo STATIC
  src/algorithms/qoi/qoi_encoder.c
)
target_include_directories(img2bin_qoi_algo
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/qoi"
)
target_link_libraries(img2bin_qoi_algo PUBLIC img2bin_core)

add_library(img2bin_indexqoimask_algo STATIC
  src/algorithms/indexqoimask/indexqoimask_encoder.c
)
target_include_directories(img2bin_indexqoimask_algo
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/indexqoimask"
)
target_link_libraries(img2bin_indexqoimask_algo PUBLIC img2bin_core)

add_library(img2bin_decoder STATIC
  src/decoder/img2bin_decode.c
)
target_include_directories(img2bin_decoder
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/decoder"
)

add_library(img2bin_raw_app STATIC
  src/apps/img2bin_raw/app.c
)
target_include_directories(img2bin_raw_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/raw"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_raw"
)
target_link_libraries(img2bin_raw_app PUBLIC img2bin_tool_app_core img2bin_raw_algo)

add_library(img2bin_imprle_app STATIC
  src/apps/img2bin_imprle/app.c
)
target_include_directories(img2bin_imprle_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/imprle"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_imprle"
)
target_link_libraries(img2bin_imprle_app PUBLIC img2bin_tool_app_core img2bin_imprle_algo)

add_library(img2bin_rle_app STATIC
  src/apps/img2bin_rle/app.c
)
target_include_directories(img2bin_rle_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/rle"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_rle"
)
target_link_libraries(img2bin_rle_app PUBLIC img2bin_tool_app_core img2bin_rle_algo)

add_library(img2bin_qoi_app STATIC
  src/apps/img2bin_qoi/app.c
)
target_include_directories(img2bin_qoi_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/qoi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_qoi"
)
target_link_libraries(img2bin_qoi_app PUBLIC img2bin_tool_app_core img2bin_qoi_algo)

add_library(img2bin_qoif_app STATIC
  src/apps/img2bin_qoif/app.c
)
target_include_directories(img2bin_qoif_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/qoi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_qoif"
)
target_link_libraries(img2bin_qoif_app PUBLIC img2bin_tool_app_core img2bin_qoi_algo)

add_library(img2bin_indexqoi_app STATIC
  src/apps/img2bin_indexqoi/app.c
)
target_include_directories(img2bin_indexqoi_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/qoi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_indexqoi"
)
target_link_libraries(img2bin_indexqoi_app PUBLIC img2bin_tool_app_core img2bin_qoi_algo)

add_library(img2bin_indexqoimask_app STATIC
  src/apps/img2bin_indexqoimask/app.c
)
target_include_directories(img2bin_indexqoimask_app
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/indexqoimask"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_indexqoimask"
)
target_link_libraries(img2bin_indexqoimask_app PUBLIC img2bin_tool_app_core img2bin_indexqoimask_algo)

add_executable(img2bin_raw
  src/apps/img2bin_raw/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_raw
    FILE_DESCRIPTION "img2bin_raw"
    INTERNAL_NAME "img2bin_raw"
    ORIGINAL_FILENAME "img2bin_raw.exe"
    PRODUCT_NAME "img2bin_raw"
    ICON "${IMG2BIN_ICON_DIR}/RAW.ico"
  )
endif()
target_link_libraries(img2bin_raw PRIVATE img2bin_raw_app)

add_executable(img2bin_imprle
  src/apps/img2bin_imprle/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_imprle
    FILE_DESCRIPTION "img2bin_imprle"
    INTERNAL_NAME "img2bin_imprle"
    ORIGINAL_FILENAME "img2bin_imprle.exe"
    PRODUCT_NAME "img2bin_imprle"
    ICON "${IMG2BIN_ICON_DIR}/RLE2.ico"
  )
endif()
target_link_libraries(img2bin_imprle PRIVATE img2bin_imprle_app)

add_executable(img2bin_rle
  src/apps/img2bin_rle/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_rle
    FILE_DESCRIPTION "img2bin_rle"
    INTERNAL_NAME "img2bin_rle"
    ORIGINAL_FILENAME "img2bin_rle.exe"
    PRODUCT_NAME "img2bin_rle"
    ICON "${IMG2BIN_ICON_DIR}/RLE.ico"
  )
endif()
target_link_libraries(img2bin_rle PRIVATE img2bin_rle_app)

add_executable(img2bin_qoi
  src/apps/img2bin_qoi/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_qoi
    FILE_DESCRIPTION "img2bin_qoi"
    INTERNAL_NAME "img2bin_qoi"
    ORIGINAL_FILENAME "img2bin_qoi.exe"
    PRODUCT_NAME "img2bin_qoi"
    ICON "${IMG2BIN_ICON_DIR}/QOI.ico"
  )
endif()
target_link_libraries(img2bin_qoi PRIVATE img2bin_qoi_app)

add_executable(img2bin_qoif
  src/apps/img2bin_qoif/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_qoif
    FILE_DESCRIPTION "img2bin_qoif"
    INTERNAL_NAME "img2bin_qoif"
    ORIGINAL_FILENAME "img2bin_qoif.exe"
    PRODUCT_NAME "img2bin_qoif"
    ICON "${IMG2BIN_ICON_DIR}/QOIF-QOIFAST.ico"
  )
endif()
target_link_libraries(img2bin_qoif PRIVATE img2bin_qoif_app)

add_executable(img2bin_indexqoi
  src/apps/img2bin_indexqoi/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_indexqoi
    FILE_DESCRIPTION "img2bin_indexqoi"
    INTERNAL_NAME "img2bin_indexqoi"
    ORIGINAL_FILENAME "img2bin_indexqoi.exe"
    PRODUCT_NAME "img2bin_indexqoi"
    ICON "${IMG2BIN_ICON_DIR}/QOII-indexQOI.ico"
  )
endif()
target_link_libraries(img2bin_indexqoi PRIVATE img2bin_indexqoi_app)

add_executable(img2bin_indexqoimask
  src/apps/img2bin_indexqoimask/main.c
)
if(WIN32)
  img2bin_add_windows_resources(
    img2bin_indexqoimask
    FILE_DESCRIPTION "img2bin_indexqoimask"
    INTERNAL_NAME "img2bin_indexqoimask"
    ORIGINAL_FILENAME "img2bin_indexqoimask.exe"
    PRODUCT_NAME "img2bin_indexqoimask"
    ICON "${IMG2BIN_ICON_DIR}/QOIM-indexQOImask.ico"
  )
endif()
target_link_libraries(img2bin_indexqoimask PRIVATE img2bin_indexqoimask_app)

add_executable(img2bin_tests
  tests/test_main.c
)
target_include_directories(img2bin_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/raw"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/imprle"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/rle"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/qoi"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/indexqoimask"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_raw"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_imprle"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_rle"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_qoi"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_qoif"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_indexqoi"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/img2bin_indexqoimask"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/decoder"
  "${IMG2BIN_THIRD_PARTY_DIR}"
)
target_compile_definitions(img2bin_tests PRIVATE IMG2BIN_SOURCE_DIR="${IMG2BIN_SOURCE_DIR_CMAKE}")
target_link_libraries(img2bin_tests PRIVATE img2bin_raw_app img2bin_imprle_app img2bin_rle_app img2bin_qoi_app img2bin_qoif_app img2bin_indexqoi_app img2bin_indexqoimask_app img2bin_decoder)
add_dependencies(img2bin_tests img2bin_raw img2bin_imprle img2bin_rle img2bin_qoi img2bin_qoif img2bin_indexqoi img2bin_indexqoimask)
if(WIN32)
  target_link_libraries(img2bin_tests PRIVATE version)
endif()

add_test(NAME img2bin_tests COMMAND img2bin_tests)
