cmake_minimum_required(VERSION 3.5)
if( EOSIO_COMPILE_TEST_CONTRACTS )
include(ExternalProject)
# if no cdt root is given use default path
if(CDT_ROOT STREQUAL "" OR NOT CDT_ROOT)
   find_package(cdt)
endif()

ExternalProject_Add(
   shard_contract_interface_tests_project
   SOURCE_DIR ${CMAKE_SOURCE_DIR}/src
   BINARY_DIR ${CMAKE_BINARY_DIR}/shard_contract_interface_tests
   CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CDT_ROOT}/lib/cmake/cdt/CDTWasmToolchain.cmake
   UPDATE_COMMAND ""
   PATCH_COMMAND ""
   TEST_COMMAND ""
   INSTALL_COMMAND ""
   BUILD_ALWAYS 1
)
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/shard_contract_interface_tests.wasm ${CMAKE_CURRENT_BINARY_DIR}/shard_contract_interface_tests.wasm COPYONLY )
endif()

