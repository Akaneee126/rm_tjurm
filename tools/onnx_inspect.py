#!/usr/bin/python3
"""Minimal ONNX protobuf scanner (no onnx package required).

Prints ir_version, opsets, graph input/output value-infos with tensor
elem_type / shape, plus an op histogram so we can judge what an NPU
converter (AIMO -> QNN) will have to swallow.
"""
import sys
from collections import Counter

ELEM = {
    1: "float32", 2: "uint8", 3: "int8", 4: "uint16", 5: "int16",
    6: "int32", 7: "int64", 8: "string", 9: "bool", 10: "float16",
    11: "float64", 12: "uint32", 13: "uint64",
}


def read_varint(buf, i):
    shift = 0
    val = 0
    while True:
        b = buf[i]
        i += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            return val, i
        shift += 7


def fields(buf):
    """Yield (field_number, wire_type, payload) for one message."""
    i = 0
    n = len(buf)
    while i < n:
        key, i = read_varint(buf, i)
        fnum = key >> 3
        wtype = key & 7
        if wtype == 0:
            val, i = read_varint(buf, i)
            yield fnum, wtype, val
        elif wtype == 1:
            yield fnum, wtype, buf[i:i + 8]
            i += 8
        elif wtype == 2:
            ln, i = read_varint(buf, i)
            yield fnum, wtype, buf[i:i + ln]
            i += ln
        elif wtype == 5:
            yield fnum, wtype, buf[i:i + 4]
            i += 4
        else:
            raise ValueError("bad wire type %d" % wtype)


def parse_value_info(buf):
    name = ""
    shape = []
    etype = None
    for f, w, p in fields(buf):
        if f == 1 and w == 2:
            name = p.decode("utf-8", "replace")
        elif f == 2 and w == 2:          # TypeProto
            for f2, w2, p2 in fields(p):
                if f2 == 1 and w2 == 2:  # tensor_type
                    for f3, w3, p3 in fields(p2):
                        if f3 == 1 and w3 == 0:
                            etype = p3
                        elif f3 == 2 and w3 == 2:   # shape
                            for f4, w4, p4 in fields(p3):
                                if f4 == 1 and w4 == 2:  # dim
                                    d = "?"
                                    for f5, w5, p5 in fields(p4):
                                        if f5 == 1 and w5 == 0:
                                            d = p5
                                        elif f5 == 2 and w5 == 2:
                                            d = p5.decode()
                                    shape.append(d)
    return name, ELEM.get(etype, etype), shape


def main(path):
    buf = open(path, "rb").read()
    graph = None
    opsets = []
    ir = None
    producer = ""
    for f, w, p in fields(buf):
        if f == 1 and w == 0:
            ir = p
        elif f == 2 and w == 2:
            producer = p.decode("utf-8", "replace")
        elif f == 7 and w == 2:
            graph = p
        elif f == 8 and w == 2:
            dom = ""
            ver = None
            for f2, w2, p2 in fields(p):
                if f2 == 1 and w2 == 2:
                    dom = p2.decode()
                elif f2 == 2 and w2 == 0:
                    ver = p2
            opsets.append((dom or "ai.onnx", ver))

    inputs, outputs, ops = [], [], Counter()
    inits = set()
    for f, w, p in fields(graph):
        if f == 1 and w == 2:
            op = ""
            for f2, w2, p2 in fields(p):
                if f2 == 4 and w2 == 2:
                    op = p2.decode()
            ops[op] += 1
        elif f == 11 and w == 2:
            inputs.append(parse_value_info(p))
        elif f == 12 and w == 2:
            outputs.append(parse_value_info(p))
        elif f == 5 and w == 2:
            for f2, w2, p2 in fields(p):
                if f2 == 8 and w2 == 2:
                    inits.add(p2.decode())

    print("== %s ==" % path)
    print("ir_version   :", ir)
    print("producer     :", producer)
    print("opsets       :", opsets)
    for label, lst in (("INPUT", inputs), ("OUTPUT", outputs)):
        for name, etype, shape in lst:
            tag = " (initializer, ignored)" if name in inits else ""
            print("%-6s: %-24s %-8s %s%s" % (label, name, etype, shape, tag))
    print("num nodes    :", sum(ops.values()))
    print("op histogram :")
    for op, c in ops.most_common():
        print("   %-18s %d" % (op, c))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
