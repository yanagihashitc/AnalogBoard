cmake_minimum_required(VERSION 3.25)

project(analogboard_p0s_zarr_prototype LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "The P0-S prototype requires Windows MSVC and CNG")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(P0S_EXPECTED_BUNDLE_NAME "analogboard-p0-s-dependencies-20260721.tar.gz")
set(P0S_EXPECTED_BUNDLE_SHA256 "4fbae209a47b050ee21f8d48091393a093e02ed396c108e90bf1cf59c0af019f")
set(P0S_EXPECTED_JSON_SHA256 "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6")
set(P0S_EXPECTED_BLOSC_HEADER_SHA256 "1b7efa0efe51294ba8eb5ff4bf6d612d1c837a8e1df977fa44f6fe61802c8e24")
set(P0S_EXPECTED_BLOSC_EXPORT_HEADER_SHA256 "ca20d39d276209d485b900675fabbb14c78642f0ddef93a0141c8a5802eadc68")
set(P0S_EXPECTED_RELEASE_LIB_SHA256 "0660daf135ccd08a3060905a35ba139f332c8e398a6ddc82b308a0deae4d70ee")
set(P0S_EXPECTED_DEBUG_LIB_SHA256 "cbeb1b05517d82adf36d787c1b0790d6d503f27749b4ff7e48edcbc3108045e6")
set(P0S_EXPECTED_GCSA_KAT_SHA256 "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa")

set(ANALOGBOARD_P0S_DEPENDENCY_BUNDLE "$ENV{ANALOGBOARD_P0S_DEPENDENCY_BUNDLE}"
    CACHE FILEPATH "Exact owner-approved offline dependency archive")
if(NOT ANALOGBOARD_P0S_DEPENDENCY_BUNDLE)
  message(FATAL_ERROR
    "ANALOGBOARD_P0S_DEPENDENCY_BUNDLE is required; expected ${P0S_EXPECTED_BUNDLE_NAME} "
    "with SHA-256 ${P0S_EXPECTED_BUNDLE_SHA256}. No download is permitted.")
endif()
get_filename_component(P0S_BUNDLE_NAME "${ANALOGBOARD_P0S_DEPENDENCY_BUNDLE}" NAME)
if(NOT P0S_BUNDLE_NAME STREQUAL P0S_EXPECTED_BUNDLE_NAME)
  message(FATAL_ERROR "Unexpected dependency archive name: ${P0S_BUNDLE_NAME}")
endif()
file(SHA256 "${ANALOGBOARD_P0S_DEPENDENCY_BUNDLE}" P0S_BUNDLE_SHA256)
if(NOT P0S_BUNDLE_SHA256 STREQUAL P0S_EXPECTED_BUNDLE_SHA256)
  message(FATAL_ERROR "Dependency archive SHA-256 mismatch: ${P0S_BUNDLE_SHA256}")
endif()

get_filename_component(P0S_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(P0S_DEPENDENCY_ROOT
    "${P0S_REPOSITORY_ROOT}/.deps/p0-s/analogboard-p0-s-dependencies-20260721"
    CACHE PATH "Verified extracted dependency root")
set(P0S_BLOSC_ROOT "${P0S_DEPENDENCY_ROOT}/c-blosc"
    CACHE PATH "c-blosc include/library root")
set(P0S_JSON_ROOT "${P0S_DEPENDENCY_ROOT}/nlohmann-json"
    CACHE PATH "nlohmann/json include root")
set(P0S_GCSA_KAT_FILE
    "${P0S_REPOSITORY_ROOT}/../gcsa/src/gcsa/store/data/aead_v1_kat.json"
    CACHE FILEPATH "Read-only accepted gcsa AEAD v1 KAT")
option(P0S_EXPECT_APPROVED_BLOSC_HASHES
       "Require the byte-pinned approved c-blosc library" ON)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR
    "CMAKE_BUILD_TYPE must be exactly Release or Debug for the P0-S prototype")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(P0S_DEFAULT_BLOSC_LIBRARY "${P0S_BLOSC_ROOT}/lib/Debug/libblosc.lib")
  set(P0S_EXPECTED_BLOSC_LIBRARY_SHA256 "${P0S_EXPECTED_DEBUG_LIB_SHA256}")
  set(P0S_REQUIRED_RUNTIME "MultiThreadedDebugDLL")
else()
  set(P0S_DEFAULT_BLOSC_LIBRARY "${P0S_BLOSC_ROOT}/lib/libblosc.lib")
  set(P0S_EXPECTED_BLOSC_LIBRARY_SHA256 "${P0S_EXPECTED_RELEASE_LIB_SHA256}")
  set(P0S_REQUIRED_RUNTIME "MultiThreadedDLL")
endif()
set(P0S_BLOSC_LIBRARY "${P0S_DEFAULT_BLOSC_LIBRARY}"
    CACHE FILEPATH "Exact c-blosc static library for this configuration")

if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "${P0S_REQUIRED_RUNTIME}")
endif()
if(NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL P0S_REQUIRED_RUNTIME)
  message(FATAL_ERROR
    "CMAKE_MSVC_RUNTIME_LIBRARY must be ${P0S_REQUIRED_RUNTIME} for ${CMAKE_BUILD_TYPE}")
endif()

set(P0S_BLOSC_HEADER "${P0S_BLOSC_ROOT}/include/blosc.h")
set(P0S_BLOSC_EXPORT_HEADER "${P0S_BLOSC_ROOT}/include/blosc-export.h")
set(P0S_JSON_HEADER "${P0S_JSON_ROOT}/include/nlohmann/json.hpp")
foreach(P0S_REQUIRED_FILE IN ITEMS
    "${P0S_BLOSC_HEADER}" "${P0S_BLOSC_EXPORT_HEADER}"
    "${P0S_JSON_HEADER}" "${P0S_BLOSC_LIBRARY}")
  if(NOT EXISTS "${P0S_REQUIRED_FILE}")
    message(FATAL_ERROR
      "Verified offline dependency is absent: ${P0S_REQUIRED_FILE}. "
      "Run scripts/validate_offline_bundle.py; no download is permitted.")
  endif()
endforeach()
if(NOT EXISTS "${P0S_GCSA_KAT_FILE}")
  message(FATAL_ERROR
    "Accepted gcsa AEAD KAT is absent: ${P0S_GCSA_KAT_FILE}; expected SHA-256 "
    "${P0S_EXPECTED_GCSA_KAT_SHA256}. No substitute or download is permitted.")
endif()

file(SHA256 "${P0S_BLOSC_HEADER}" P0S_BLOSC_HEADER_SHA256)
file(SHA256 "${P0S_BLOSC_EXPORT_HEADER}" P0S_BLOSC_EXPORT_HEADER_SHA256)
file(SHA256 "${P0S_JSON_HEADER}" P0S_JSON_HEADER_SHA256)
file(SHA256 "${P0S_GCSA_KAT_FILE}" P0S_GCSA_KAT_SHA256)
if(NOT P0S_BLOSC_HEADER_SHA256 STREQUAL P0S_EXPECTED_BLOSC_HEADER_SHA256)
  message(FATAL_ERROR "blosc.h SHA-256 mismatch: ${P0S_BLOSC_HEADER_SHA256}")
endif()
if(NOT P0S_BLOSC_EXPORT_HEADER_SHA256 STREQUAL P0S_EXPECTED_BLOSC_EXPORT_HEADER_SHA256)
  message(FATAL_ERROR
    "blosc-export.h SHA-256 mismatch: ${P0S_BLOSC_EXPORT_HEADER_SHA256}")
endif()
if(NOT P0S_JSON_HEADER_SHA256 STREQUAL P0S_EXPECTED_JSON_SHA256)
  message(FATAL_ERROR "json.hpp SHA-256 mismatch: ${P0S_JSON_HEADER_SHA256}")
endif()
if(NOT P0S_GCSA_KAT_SHA256 STREQUAL P0S_EXPECTED_GCSA_KAT_SHA256)
  message(FATAL_ERROR "gcsa AEAD KAT SHA-256 mismatch: ${P0S_GCSA_KAT_SHA256}")
endif()
set(P0S_GCSA_KAT_COPY "${CMAKE_CURRENT_BINARY_DIR}/aead_v1_kat.json")
configure_file("${P0S_GCSA_KAT_FILE}" "${P0S_GCSA_KAT_COPY}" COPYONLY)
if(P0S_EXPECT_APPROVED_BLOSC_HASHES)
  file(SHA256 "${P0S_BLOSC_LIBRARY}" P0S_BLOSC_LIBRARY_SHA256)
  if(NOT P0S_BLOSC_LIBRARY_SHA256 STREQUAL P0S_EXPECTED_BLOSC_LIBRARY_SHA256)
    message(FATAL_ERROR "libblosc.lib SHA-256 mismatch: ${P0S_BLOSC_LIBRARY_SHA256}")
  endif()
endif()

add_library(p0s_zarr_adapters STATIC
  src/aead_store.cpp
  src/blosc_adapter.cpp
  src/strict_json.cpp)
target_include_directories(p0s_zarr_adapters
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${P0S_JSON_ROOT}/include"
  PRIVATE "${P0S_BLOSC_ROOT}/include")
target_compile_definitions(p0s_zarr_adapters PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(p0s_zarr_adapters PRIVATE /W4 /permissive-)
target_link_libraries(p0s_zarr_adapters PRIVATE "${P0S_BLOSC_LIBRARY}" bcrypt)

add_library(p0s_minimal_zarr_writer STATIC
  src/atomic_file.cpp
  src/minimal_zarr_writer.cpp)
target_include_directories(p0s_minimal_zarr_writer
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(p0s_minimal_zarr_writer
  PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(p0s_minimal_zarr_writer PRIVATE /W4 /permissive-)
target_link_libraries(p0s_minimal_zarr_writer PRIVATE p0s_zarr_adapters bcrypt)

add_executable(p0s_adapter_tests tests/adapter_tests.cpp)
target_include_directories(p0s_adapter_tests
  PRIVATE "${P0S_BLOSC_ROOT}/include")
target_compile_options(p0s_adapter_tests PRIVATE /W4 /permissive-)
target_link_libraries(p0s_adapter_tests PRIVATE p0s_zarr_adapters "${P0S_BLOSC_LIBRARY}")

add_executable(p0s_kat_matrix_tests tests/kat_matrix_tests.cpp)
target_include_directories(p0s_kat_matrix_tests
  PRIVATE "${P0S_BLOSC_ROOT}/include")
target_compile_options(p0s_kat_matrix_tests PRIVATE /W4 /permissive-)
target_link_libraries(p0s_kat_matrix_tests PRIVATE p0s_zarr_adapters)

add_executable(p0s_store_writer_tests tests/store_writer_tests.cpp)
target_include_directories(p0s_store_writer_tests
  PRIVATE "${P0S_JSON_ROOT}/include")
target_compile_options(p0s_store_writer_tests PRIVATE /W4 /permissive-)
target_link_libraries(p0s_store_writer_tests PRIVATE p0s_minimal_zarr_writer)

add_executable(p0s_store_generator tools/store_generator.cpp)
target_compile_options(p0s_store_generator PRIVATE /W4 /permissive-)
target_link_libraries(p0s_store_generator PRIVATE p0s_minimal_zarr_writer)

enable_testing()
add_test(NAME p0s_adapter_tests COMMAND p0s_adapter_tests)
add_test(NAME p0s_kat_matrix_tests
         COMMAND p0s_kat_matrix_tests "${P0S_GCSA_KAT_COPY}")
add_test(NAME p0s_store_writer_tests
         COMMAND p0s_store_writer_tests "${P0S_GCSA_KAT_COPY}")

message(STATUS "P0-S dependency archive SHA-256: ${P0S_BUNDLE_SHA256}")
message(STATUS "P0-S c-blosc root: ${P0S_BLOSC_ROOT}")
message(STATUS "P0-S CNG backend: bcrypt")
