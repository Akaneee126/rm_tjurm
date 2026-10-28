#!/bin/sh
cd /etc/openrm
sed -i 's/"FPS" : 120/"FPS" : 30/' Config.json
rm -f debug/*.jpg
TJURM-2024 >/tmp/run2.log 2>&1 &
PID=$!
sleep 4
echo "=== per-thread CPU (top -H) ==="
top -bn1 -H -p $PID 2>/dev/null | tail -20
echo
echo "=== overall ==="
top -bn1 -p $PID 2>/dev/null | tail -4
echo
echo "=== images written in ~6s ==="
sleep 2
ls debug/*.jpg 2>/dev/null | wc -l
kill $PID 2>/dev/null
wait $PID 2>/dev/null
echo "=== log ==="
head -12 /tmp/run2.log
