#!/bin/sh
cd /etc/openrm
N=$(timeout 6 TJURM-2024 2>&1 | grep -c "dbg pre")
FRAMES=$((N * 10))
echo "dbg lines: $N  -> frames in 6s: $FRAMES"
awk -v f="$FRAMES" 'BEGIN { printf "pipeline: %.1f fps (%.1f ms/frame)\n", f/6.0, 6000.0/f }'
