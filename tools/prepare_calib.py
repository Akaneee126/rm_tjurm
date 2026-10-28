#!/usr/bin/python3
"""把原始赛场图批量 letterbox 成模型输入尺寸，用于 AIMO 的 INT8 校准集。

用法:
    python3 tools/prepare_calib.py raw/ calib/ --width 640 --height 640 --pad 114

要点（与 C++ 端 rm::letterboxToTensor 完全一致）:
  1. 等比缩放，短边贴合，剩余部分居中填充
  2. 填充值 114（与训练时一致）
  3. 不改变通道顺序（AIMO 会根据 ONNX 的输入定义处理 RGB/BGR）

建议:
  * 200~400 张，覆盖远/中/近距离、红蓝、灯光/逆光/暗场、部分遮挡、运动模糊
  * 约 1/5 是"画面里没有装甲板"的负样本
  * 不要用连拍中几乎相同的帧凑数，多样性比数量重要
"""
import argparse
import glob
import os

import cv2
import numpy as np


def letterbox(img, dw, dh, pad):
    h, w = img.shape[:2]
    scale = min(dw / w, dh / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((dh, dw, 3), pad, np.uint8)
    x0, y0 = (dw - nw) // 2, (dh - nh) // 2
    canvas[y0:y0 + nh, x0:x0 + nw] = resized
    return canvas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=640)
    ap.add_argument("--pad", type=int, default=114)
    args = ap.parse_args()

    os.makedirs(args.dst, exist_ok=True)
    files = []
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
        files += glob.glob(os.path.join(args.src, ext))

    if not files:
        print("no images found in", args.src)
        return

    ok = 0
    for i, path in enumerate(sorted(files)):
        img = cv2.imread(path)
        if img is None:
            print("skip unreadable:", path)
            continue
        out = letterbox(img, args.width, args.height, args.pad)
        cv2.imwrite(os.path.join(args.dst, "%04d.jpg" % i), out, [cv2.IMWRITE_JPEG_QUALITY, 95])
        ok += 1

    print("done: %d/%d -> %s (%dx%d)" % (ok, len(files), args.dst, args.width, args.height))


if __name__ == "__main__":
    main()
