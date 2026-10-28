#!/bin/sh
echo "===== INSTALL OpenRM ====="
cd /root/OpenRM-main
cmake --install build > /tmp/openrm_install.log 2>&1 || { tail -20 /tmp/openrm_install.log; exit 1; }
ldconfig
ls -l /usr/local/lib/libopenrm_infer.so /usr/local/lib/libopenrm_*.so 2>/dev/null | head -20
echo "-- cmake config --"
ls /usr/local/lib/cmake/openrm/
echo "-- headers --"
ls /usr/local/include/openrm/infer/

echo "===== CONFIGURE TJURM ====="
cd /root/TJURM-main
find . -name "*.sh" -exec sed -i 's/\r$//' {} \; 2>/dev/null
rm -rf build && mkdir -p build
cmake -S . -B build > /tmp/tjurm_cmake.log 2>&1
rc=$?
tail -20 /tmp/tjurm_cmake.log
echo "configure rc=$rc"
[ $rc -ne 0 ] && exit $rc

echo "===== BUILD TJURM ====="
cmake --build build -j8 > /tmp/tjurm_build.log 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "--- errors ---"
    grep -E "error:|undefined reference" /tmp/tjurm_build.log | head -40
    echo "--- tail ---"
    tail -15 /tmp/tjurm_build.log
else
    echo "build OK"
    ls -l build/TJURM-2024
fi
exit $rc
