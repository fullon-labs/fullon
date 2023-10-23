cmake_minimum_required(VERSION 3.5)
if( EOSIO_COMPILE_TEST_CONTRACTS )
   add_contract( shard_contract_interface_tests shard_contract_interface_tests shard_contract_interface_tests.cpp )
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/shard_contract_interface_tests.wasm ${CMAKE_CURRENT_BINARY_DIR}/shard_contract_interface_tests.wasm COPYONLY )
endif()

