#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense OR 0BSD
"""Assertions about the built cobs.wasm: claims the build enforces, not documents.

Standard library only, like release.py, so it needs no toolchain.
"""

import argparse
import sys

SECTION_NAMES = {
    0: "custom", 1: "type", 2: "import", 3: "function", 4: "table", 5: "memory",
    6: "global", 7: "export", 8: "start", 9: "element", 10: "code", 11: "data",
    12: "datacount",
}

EXTERNAL_KINDS = {0: "func", 1: "table", 2: "memory", 3: "global"}

EXPECTED_EXPORTS = {
    "memory", "cobs_wasm_encode", "cobs_wasm_decode",
    "cobs_wasm_heap_base", "cobs_wasm_abi_version", "cobs_wasm_nop",
}


class Reader:
    def __init__(self, buf):
        self.buf, self.pos = buf, 0

    def u8(self):
        b = self.buf[self.pos]
        self.pos += 1
        return b

    def uleb(self):
        result, shift = 0, 0
        while True:
            b = self.u8()
            result |= (b & 0x7F) << shift
            if not b & 0x80:
                return result
            shift += 7

    def name(self):
        n = self.uleb()
        s = self.buf[self.pos:self.pos + n].decode("utf-8")
        self.pos += n
        return s


def parse(buf):
    r = Reader(buf)
    if buf[:4] != b"\0asm":
        raise ValueError("not a wasm module: bad magic")
    r.pos = 4
    version = int.from_bytes(buf[4:8], "little")
    if version != 1:
        raise ValueError(f"unexpected wasm version {version}, want 1")
    r.pos = 8

    sections, imports, exports, memories = [], [], [], []
    while r.pos < len(buf):
        sid = r.u8()
        size = r.uleb()
        end = r.pos + size
        sections.append((sid, size))

        if sid == 2:  # import
            for _ in range(r.uleb()):
                module, field = r.name(), r.name()
                kind = r.u8()
                imports.append(f"{module}.{field} ({EXTERNAL_KINDS.get(kind, kind)})")
                break  # only the count matters; bail before the type payload
        elif sid == 5:  # memory
            for _ in range(r.uleb()):
                flags = r.uleb()
                initial = r.uleb()
                maximum = r.uleb() if flags & 1 else None
                memories.append((initial, maximum))
        elif sid == 7:  # export
            for _ in range(r.uleb()):
                field = r.name()
                r.u8()      # kind
                r.uleb()    # index
                exports.append(field)

        r.pos = end
    return sections, imports, exports, memories


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wasm")
    ap.add_argument("--size-max", type=int, required=True)
    ap.add_argument("--initial-pages", type=int, default=4)
    args = ap.parse_args()

    buf = open(args.wasm, "rb").read()
    sections, imports, exports, memories = parse(buf)
    present = {SECTION_NAMES.get(sid, sid) for sid, _ in sections}
    failures = []

    # The wasm analogue of `make size-nolibc`, and it means no import object.
    if imports:
        failures.append(f"expected zero imports, found: {', '.join(imports)}")

    if set(exports) != EXPECTED_EXPORTS:
        extra = set(exports) - EXPECTED_EXPORTS
        missing = EXPECTED_EXPORTS - set(exports)
        detail = []
        if extra:
            detail.append(f"unexpected {sorted(extra)}")
        if missing:
            detail.append(f"missing {sorted(missing)}")
        failures.append("export set wrong: " + "; ".join(detail))

    if len(memories) != 1:
        failures.append(f"expected exactly 1 memory, found {len(memories)}")
    else:
        initial, maximum = memories[0]
        if initial != args.initial_pages:
            failures.append(f"memory initial is {initial} pages, want {args.initial_pages}")
        if maximum is not None:
            failures.append(f"memory declares a maximum ({maximum}); growth policy "
                            "belongs to the engine")

    # cobs.c has no globals or statics; carry that into the artifact.
    if "data" in present:
        failures.append("data section present: something acquired static storage")
    if "start" in present:
        failures.append("start section present: something wants to run at instantiation")

    if len(buf) > args.size_max:
        failures.append(f"{len(buf)} bytes exceeds budget of {args.size_max}")

    print(f"{args.wasm}: {len(buf)} bytes (budget {args.size_max}), "
          f"{len(imports)} imports, {len(exports)} exports, "
          f"memory {memories[0][0] if memories else '?'} pages, "
          f"sections: {' '.join(sorted(present))}")

    if failures:
        for f in failures:
            print(f"  FAIL: {f}", file=sys.stderr)
        return 1
    print("  all assertions passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
