#!/usr/bin/env python3
"""Offline reader for the id Tech 7 static type-info tables in DOOMEternalx64vk.exe.

The exe carries two generated tables of class records (engine and game). Each class
record is 0x48 bytes:

    +0x00 char*  name
    +0x08 char*  superType          ("" when none)
    +0x10 int32  size               (-1 for opaque types)
    +0x20 classVariableInfo_t* variables (NULL-terminated array)
    +0x28 uint64* variableNameHashes
    +0x30..+0x40 createInstance, createModel, metaData

Each variable record is 0x48 bytes:

    +0x00 char* type, +0x08 char* ops ("*", "[N]", ...), +0x10 char* name,
    +0x18 int32 offset, +0x1C int32 size, +0x20 uint64 flags, +0x28 char* comment,
    +0x30..+0x40 get, set, reallocate

The tables are found by scanning the image for runs of records whose pointers resolve
to printable strings, so no build-specific address is needed. Pointers are read as
absolute VAs against the PE ImageBase (the exe is not relocated on disk).

Usage:
    extract_typeinfo.py EXE --json out.json
    extract_typeinfo.py EXE --class idPlayer --grep "view|fov"
"""
import argparse
import json
import re
import struct
import sys

import numpy as np
import pefile

REC = 0x48


class Image:
    def __init__(self, path):
        pe = pefile.PE(path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.timestamp = pe.FILE_HEADER.TimeDateStamp
        raw = open(path, "rb").read()
        mem = bytearray(self.size)
        self.sections = {}
        for s in pe.sections:
            n = min(s.SizeOfRawData, s.Misc_VirtualSize)
            mem[s.VirtualAddress:s.VirtualAddress + n] = raw[s.PointerToRawData:s.PointerToRawData + n]
            self.sections[s.Name.rstrip(b"\0").decode()] = (s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
        self.mem = bytes(mem)
        self.qwords = np.frombuffer(self.mem[: self.size // 8 * 8], dtype="<u8")

    def u64(self, rva):
        return struct.unpack_from("<Q", self.mem, rva)[0]

    def i32(self, rva):
        return struct.unpack_from("<i", self.mem, rva)[0]

    def ptr(self, rva):
        v = self.u64(rva)
        return v - self.base if self.base <= v < self.base + self.size else None

    def cstr(self, rva, limit=4096):
        if rva is None:
            return ""
        end = self.mem.find(b"\0", rva, rva + limit)
        if end < 0:
            end = rva + limit
        return self.mem[rva:end].decode("latin1")


def _printable_name(im, rva):
    if rva is None:
        return False
    s = im.cstr(rva, 512)
    return all(32 <= ord(c) < 127 for c in s)


def is_class_record(im, rva):
    name, sup = im.ptr(rva), im.ptr(rva + 8)
    if name is None or sup is None or im.u64(rva + 16) >> 32:
        return False
    if im.u64(rva + 32) and im.ptr(rva + 32) is None:
        return False
    return _printable_name(im, name) and _printable_name(im, sup) and im.cstr(name, 512) != ""


def find_class_tables(im, min_run=8):
    lo, hi = im.sections[".rdata"]
    q = im.qwords
    in_rdata = (q >= im.base + lo) & (q < im.base + hi)
    n = len(q)
    cand = np.nonzero(in_rdata[: n - 9] & in_rdata[1: n - 8] & ((q[2: n - 7] >> np.uint64(32)) == 0))[0]
    cand_set = set(int(c) for c in cand)
    seen, tables = set(), []
    for c in sorted(cand_set):
        if c in seen:
            continue
        run, k = 0, c
        while k in cand_set and is_class_record(im, k * 8):
            seen.add(k)
            run += 1
            k += REC // 8
        if run >= min_run:
            tables.append((c * 8, k * 8))
    return tables


def read_variables(im, rva):
    out = []
    while rva is not None:
        t, nm = im.ptr(rva), im.ptr(rva + 16)
        if t is None or nm is None:
            break
        out.append({
            "type": im.cstr(t),
            "ops": im.cstr(im.ptr(rva + 8)),
            "name": im.cstr(nm),
            "offset": im.i32(rva + 24),
            "size": im.i32(rva + 28),
            "flags": im.u64(rva + 32),
            "comment": im.cstr(im.ptr(rva + 40)),
        })
        rva += REC
    return out


def read_classes(im):
    classes = {}
    for start, end in find_class_tables(im):
        for rec in range(start, end, REC):
            name = im.cstr(im.ptr(rec))
            classes.setdefault(name, {
                "name": name,
                "super": im.cstr(im.ptr(rec + 8)),
                "size": im.i32(rec + 16),
                "record_rva": rec,
                "table_rva": start,
                "variables": read_variables(im, im.ptr(rec + 32)),
            })
    return classes


def print_class(classes, name, pattern=None, inherited=True):
    chain = []
    while name and name in classes:
        chain.append(classes[name])
        name = classes[name]["super"] if inherited else None
    for c in chain:
        print("%s : %s  size=0x%x" % (c["name"], c["super"] or "-", c["size"] & 0xFFFFFFFF))
        for v in c["variables"]:
            if pattern and not re.search(pattern, v["name"] + " " + v["type"], re.I):
                continue
            print("  +0x%-7x 0x%-5x %-44s %-4s %s" % (v["offset"], v["size"] & 0xFFFFFFFF, v["type"][:44], v["ops"], v["name"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("--json", help="write every class with its variables to this file")
    ap.add_argument("--class", dest="cls", action="append", help="print a class (repeatable)")
    ap.add_argument("--grep", help="regex filter for printed variables (name or type)")
    ap.add_argument("--no-inherited", action="store_true", help="do not walk superclasses")
    a = ap.parse_args()
    im = Image(a.exe)
    tables = find_class_tables(im)
    classes = read_classes(im)
    print("image size 0x%x, PE timestamp %d, %d class tables, %d classes" % (im.size, im.timestamp, len(tables), len(classes)), file=sys.stderr)
    for s, e in tables:
        print("  table rva 0x%x..0x%x (%d records)" % (s, e, (e - s) // REC), file=sys.stderr)
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"image_size": im.size, "pe_timestamp": im.timestamp,
                       "tables": tables, "classes": classes}, f, indent=0)
    for c in a.cls or []:
        print_class(classes, c, a.grep, not a.no_inherited)


if __name__ == "__main__":
    main()
