#!/bin/sh
cd /root/smoke
LD_LIBRARY_PATH=/usr/local/lib ./hik_probe 2>&1 | head -14
