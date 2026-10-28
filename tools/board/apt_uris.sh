#!/bin/sh
echo "== sources =="
cat /etc/apt/sources.list 2>/dev/null | grep -v '^#' | grep -v '^$'
ls /etc/apt/sources.list.d/ 2>/dev/null
for f in /etc/apt/sources.list.d/*; do [ -f "$f" ] && { echo "--- $f"; grep -v '^#' "$f" | grep -v '^$'; }; done
echo "== uris =="
apt-get install --print-uris -y --reinstall libeigen3-dev libceres-dev libncurses-dev libtinfo-dev 2>/dev/null | grep -o "'http[^']*'" | tr -d "'" | sort -u
