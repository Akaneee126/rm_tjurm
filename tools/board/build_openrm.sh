#!/bin/sh
cd /root/OpenRM-main || exit 1
find . -name "*.sh" -exec sed -i 's/\r$//' {} \; 2>/dev/null
rm -rf build
mkdir -p build
echo "===== CMAKE CONFIGURE ====="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /tmp/openrm_cmake.log 2>&1
rc=$?
tail -45 /tmp/openrm_cmake.log
echo "configure rc=$rc"
[ $rc -ne 0 ] && exit $rc
echo "===== BUILD ====="
cmake --build build -j8 > /tmp/openrm_build.log 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "--- errors ---"
    grep -E "error:|Error [0-9]|undefined reference" /tmp/openrm_build.log | head -40
    echo "--- tail ---"
    tail -25 /tmp/openrm_build.log
else
    echo "build OK"
    tail -5 /tmp/openrm_build.log
fi
exit $rc
