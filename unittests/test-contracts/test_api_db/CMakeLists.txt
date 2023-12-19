if( EOSIO_COMPILE_TEST_CONTRACTS )
   add_contract( test_api_db test_api_db test_api_db.cpp )


   add_contract( test_api_db test_api_shared_db test_api_db.cpp )

   target_compile_definitions(test_api_shared_db PUBLIC TEST_SHARED_TABLE)
else()
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/test_api_db.wasm ${CMAKE_CURRENT_BINARY_DIR}/test_api_db.wasm COPYONLY )
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/test_api_db.abi  ${CMAKE_CURRENT_BINARY_DIR}/test_api_db.abi  COPYONLY )

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/test_api_shared_db.wasm ${CMAKE_CURRENT_BINARY_DIR}/test_api_shared_db.wasm COPYONLY )
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/test_api_shared_db.abi  ${CMAKE_CURRENT_BINARY_DIR}/test_api_shared_db.abi  COPYONLY )
endif()
