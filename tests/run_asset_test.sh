#!/bin/sh
# Host build of the real asset_manager.cpp (stdio backend) + leak/exhaustion regression test.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
g++ -std=gnu++17 -O1 -w -DDATA_PATH="\"$T/\"" -Isource -Ilib -Itests/host_stubs \
    tests/asset_leak_test.cpp source/reimpl/asset_manager.cpp -o "$T/asset_test" -lpthread
"$T/asset_test"; rc=$?
g++ -std=gnu++17 -O1 -w -DDATA_PATH="\"$T/f/\"" -Isource -Ilib -include tests/asset_fault_backend.h \
    tests/asset_fault_test.cpp source/reimpl/asset_manager.cpp -o "$T/asset_fault_test" -lpthread
"$T/asset_fault_test" || rc=1
rm -rf "$T"
exit $rc
