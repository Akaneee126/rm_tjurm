#!/bin/sh
echo "== net =="
ip -4 addr show | grep -E "inet " | grep -v 127.0.0.1
echo "-- route --"
ip route | head -5
echo "-- dns/ping --"
cat /etc/resolv.conf 2>/dev/null | grep -v '^#'
timeout 6 ping -c 1 -W 2 223.5.5.5 >/dev/null 2>&1 && echo "ping 223.5.5.5 OK" || echo "ping FAIL"
timeout 8 apt-get -qq update >/tmp/aptupd.log 2>&1 && echo "apt update OK" || { echo "apt update FAIL"; tail -3 /tmp/aptupd.log; }
echo "== apt candidates =="
apt-cache policy libeigen3-dev libceres-dev libncurses-dev 2>/dev/null | grep -E "^[a-z]|Candidate"
echo "== aidlite.hpp: key symbols =="
grep -n "class Interpreter\b\|set_input_tensor\|get_output_tensor\|set_model_properties\|build_interpreter_from_model_and_config\|create_instance" /usr/local/include/aidlux/aidlite/aidlite.hpp | head -40
echo "== aidlite.hpp: aidem/encrypt =="
grep -in "aidem\|encrypt\|decrypt\|license" /usr/local/include/aidlux/aidlite/aidlite.hpp | head -20
echo "== aidlite.hpp: namespace =="
grep -n "^namespace\|^}" /usr/local/include/aidlux/aidlite/aidlite.hpp | head -10
