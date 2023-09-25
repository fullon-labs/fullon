project(shard_contract_interface_tests)

find_package(cdt)

add_contract( shard_contract_interface_tests shard_contract_interface_tests shard_contract_interface_tests.cpp )
target_include_directories( shard_contract_interface_tests PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( shard_contract_interface_tests ${CMAKE_SOURCE_DIR}/../ricardian )