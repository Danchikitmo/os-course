cmake_minimum_required(VERSION 3.15)


add_executable(vtsh
    ${CMAKE_SOURCE_DIR}/src/mysh.c
    ${CMAKE_SOURCE_DIR}/lib/vtsh.c
)


target_include_directories(vtsh PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
)


set_target_properties(vtsh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
