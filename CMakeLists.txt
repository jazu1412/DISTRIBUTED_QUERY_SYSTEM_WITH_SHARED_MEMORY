cmake_minimum_required(VERSION 3.14)
# Disable OpenMP
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")

string(REPLACE "-fopenmp" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
project(Mini2BasecampAltOverlay CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)  


include_directories(
    ${GRPC_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR} 
    include
)


link_directories(
    ${GRPC_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)


find_program(PROTOC protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)


add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/basecamp.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/basecamp.pb.h
           ${CMAKE_CURRENT_BINARY_DIR}/basecamp.grpc.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/basecamp.grpc.pb.h
    COMMAND ${PROTOC}
    ARGS -I=${CMAKE_CURRENT_SOURCE_DIR}/proto
         --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/basecamp.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/basecamp.proto
    COMMENT "Generating protobuf and gRPC files"
)

add_executable(basecamp_node
    src/main.cc
    src/node.cc
    src/shm_utils.cc
    src/data_manager.cc
    ${CMAKE_CURRENT_BINARY_DIR}/basecamp.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/basecamp.grpc.pb.cc
)

# Link libraries – must include absl sub-libraries used by gRPC/Protobuf
target_link_libraries(basecamp_node
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    nlohmann_json::nlohmann_json

  
    absl::base
    absl::strings
    absl::time
    absl::synchronization

   
    absl::log
    absl::cord
    absl::symbolize
    absl::stacktrace
    absl::debugging
)


set(FILTERED_GRPC_CFLAGS "")
foreach(FLAG ${GRPC_CFLAGS_OTHER})
    if(NOT "${FLAG}" STREQUAL "-fopenmp")
        list(APPEND FILTERED_GRPC_CFLAGS ${FLAG})
    endif()
endforeach()

set(FILTERED_PROTOBUF_CFLAGS "")
foreach(FLAG ${PROTOBUF_CFLAGS_OTHER})
    if(NOT "${FLAG}" STREQUAL "-fopenmp")
        list(APPEND FILTERED_PROTOBUF_CFLAGS ${FLAG})
    endif()
endforeach()

target_compile_options(basecamp_node PRIVATE
    ${FILTERED_GRPC_CFLAGS}
    ${FILTERED_PROTOBUF_CFLAGS}
)
