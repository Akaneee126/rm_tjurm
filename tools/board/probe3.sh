#!/bin/sh
echo "== headers present? =="
ls -d /usr/include/eigen3 2>/dev/null || echo "no /usr/include/eigen3"
ls /usr/include/ceres 2>/dev/null | head -5 || echo "no /usr/include/ceres"
ls /usr/include/ncurses.h /usr/include/curses.h 2>/dev/null || echo "no curses headers"
ls /usr/local/include/eigen3 /usr/local/include/ceres 2>/dev/null || echo "no /usr/local eigen/ceres"
echo "== any eigen/ceres anywhere =="
find /usr/include /usr/local/include -maxdepth 3 -name "Eigen" -o -maxdepth 3 -name "ceres" 2>/dev/null | head
echo "== gflags/glog/suitesparse =="
dpkg -l 2>/dev/null | grep -E "gflags|glog|suitesparse|lapack" | awk '{print $2, $3}'
echo "== aidlite.hpp: Interpreter methods =="
sed -n '400,470p' /usr/local/include/aidlux/aidlite/aidlite.hpp
echo "== aidlite.hpp: set_model_properties full =="
sed -n '266,280p' /usr/local/include/aidlux/aidlite/aidlite.hpp
echo "== aidlite.hpp: Config fields =="
sed -n '355,400p' /usr/local/include/aidlux/aidlite/aidlite.hpp
