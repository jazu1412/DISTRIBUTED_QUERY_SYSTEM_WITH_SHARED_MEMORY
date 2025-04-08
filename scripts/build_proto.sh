#!/usr/bin/env bash

protoc -I=../proto \
    --cpp_out=../build \
    --grpc_out=../build \
    --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
    ../proto/basecamp.proto

echo "Protos generated into ../build/"
