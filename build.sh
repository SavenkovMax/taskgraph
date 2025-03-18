#!/bin/sh

set -e

mkdir -p build
cd build

cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..

make

echo "The project is built successfully!"
