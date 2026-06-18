# C ABI self-test: dlopen the built plugin and run probe/create/compute.
add_executable(abi_selftest abi_selftest.c)

target_include_directories(abi_selftest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(abi_selftest PRIVATE ${CMAKE_DL_LIBS})

# Point the test at the freshly-built shared object.
target_compile_definitions(abi_selftest PRIVATE
    UNLOOK_FPGA_LIB_PATH="$<TARGET_FILE:unlook_fpga_backend>")

add_test(NAME abi_selftest COMMAND abi_selftest)
