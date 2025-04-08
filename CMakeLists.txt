cmake_minimum_required(VERSION 3.14)
# Disable OpenMP
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
project(Mini2BasecampAltOverlay CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)   # Use CMake config for Abseil

# Include directories
include_directories(
    ${GRPC_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}  # location of generated *.pb.h
    include
)

# Link directories
link_directories(
    ${GRPC_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)

# Add executable
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

    # core abseil libs:
    absl::base
    absl::strings
    absl::time
    absl::synchronization

    # logging + debugging used by recent gRPC/Protobuf
    absl::log
    absl::cord
    absl::symbolize
    absl::stacktrace
    absl::debugging
)

# Filter out OpenMP flags from CFLAGS_OTHER
set(FILTERED_GRPC_CFLAGS)
foreach(FLAG ${GRPC_CFLAGS_OTHER})
    if(NOT "${FLAG}" MATCHES "-fopenmp")
        list(APPEND FILTERED_GRPC_CFLAGS ${FLAG})
    endif()
endforeach()

set(FILTERED_PROTOBUF_CFLAGS)
foreach(FLAG ${PROTOBUF_CFLAGS_OTHER})
    if(NOT "${FLAG}" MATCHES "-fopenmp")
        list(APPEND FILTERED_PROTOBUF_CFLAGS ${FLAG})
    endif()
endforeach()

# Add compile options without OpenMP
target_compile_options(basecamp_node PRIVATE
    ${FILTERED_GRPC_CFLAGS}
    ${FILTERED_PROTOBUF_CFLAGS}
)
