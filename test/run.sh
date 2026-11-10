#!/bin/sh
# Tests of the core on the PC. CXX: any C++17 compiler, by default g++.
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
mkdir -p test/bin
$CXX -std=c++17 -Wall -Wextra -Wno-unused-parameter -Ilib/donoff2/src test/test_core.cpp -o test/bin/test_core
test/bin/test_core
