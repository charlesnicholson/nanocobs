"""Run the differential probe on an emulated 16- or 32-bit target.

Every host that runs the doctest suite has a 64-bit size_t, so nothing there
exercises COBS_SWAR_WORD_BITS at 16 or 32 on a machine whose registers are
actually that wide. Needs the cross compiler and qemu-system-{avr,arm}.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time

_TIMEOUT_SEC = 300

_TARGETS = {
    # atmega2560: 8-bit registers, 16-bit int and uintptr_t. SWAR defaults off
    # here, so --word-bits 16 is what exercises the narrow-word promotion path.
    "avr": {
        "cc": "avr-gcc",
        "cflags": ["-mmcu=atmega2560"],
        "ldflags": [],
        "io": "io_avr.c",
        "qemu": ["qemu-system-avr", "-machine", "mega2560", "-nographic",
                 "-serial", "mon:stdio", "-bios"],
    },
    # lm3s6965evb is a Cortex-M3: 32-bit, ARMv7-M, so unaligned access and the
    # 32-bit SWAR default are both live. io_cm3.c supplies the vector table and
    # startup; without them the image never reaches its reset vector.
    "cm3": {
        "cc": "arm-none-eabi-gcc",
        "cflags": ["-mcpu=cortex-m3", "-mthumb"],
        "ldflags": ["-nostartfiles", "-nostdlib", "-Tcm3.ld"],
        "io": "io_cm3.c",
        "qemu": ["qemu-system-arm", "-M", "lm3s6965evb", "-nographic",
                 "-semihosting-config", "enable=on,target=native", "-kernel"],
    },
}


def _git_root() -> pathlib.Path:
    cur = pathlib.Path(__file__).resolve()
    while cur != cur.parent:
        if (cur / ".git").exists():  # dir for a normal clone, file for a worktree
            return cur
        cur = cur.parent
    msg = f"{__file__} not in a git repo"
    raise ValueError(msg)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", required=True, choices=sorted(_TARGETS))
    ap.add_argument("--word-bits", type=int, choices=[8, 16, 32, 64],
                    help="force COBS_SWAR_WORD_BITS instead of letting cobs.c pick")
    ap.add_argument("--expect-fail", action="store_true",
                    help="build the probe with a seeded mismatch and require it to fail")
    args = ap.parse_args()
    t = _TARGETS[args.target]

    root = _git_root()
    here = pathlib.Path(__file__).resolve().parent
    cc_name = t["cc"]
    deployed = root / "bin" / cc_name
    cc_bin = str(deployed) if deployed.exists() else cc_name
    build = root / "build"
    build.mkdir(parents=True, exist_ok=True)
    elf = build / f"probe_{args.target}.elf"

    word = ["-DCOBS_SWAR_WORD_BITS=" + str(args.word_bits)] if args.word_bits else []
    if args.expect_fail:
        word.append("-DCOBS_PROBE_FAULT=1")
    cc = [cc_bin, *t["cflags"], *t["ldflags"], f"-L{here}", "-Os", "-std=c99", "-Wall",
          "-Wextra", "-Werror", "-Wconversion", f"-I{root}", "-o", str(elf),
          str(here / "probe.c"), str(here / t["io"]), str(root / "cobs.c"),
          *word]
    # The scalar half of the differential.
    ref = build / f"probe_{args.target}_ref.o"
    cc_ref = [cc_bin, *t["cflags"], "-Os", "-std=c99", "-Wall", "-Wextra", "-Werror",
              "-Wconversion", f"-I{root}",
              "-c", str(root / "tests" / "cobs_ref.c"), "-o", str(ref)]

    print(" ".join(cc_ref), flush=True)
    subprocess.run(cc_ref, check=True)
    cc.append(str(ref))
    print(" ".join(cc), flush=True)
    subprocess.run(cc, check=True)

    qemu = [*t["qemu"], str(elf)]
    print(" ".join(qemu), flush=True)
    if shutil.which(qemu[0]) is None:
        print(f"FAILED: {qemu[0]} not on PATH", file=sys.stderr)
        return 1

    # A guest that cannot halt qemu spins after printing, so read incrementally and
    # stop at DONE rather than waiting out the timeout.
    proc = subprocess.Popen(qemu, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + _TIMEOUT_SEC
    chunks = []
    try:
        assert proc.stdout is not None
        os.set_blocking(proc.stdout.fileno(), False)
        while time.monotonic() < deadline:
            got = proc.stdout.read()
            if got:
                chunks.append(got)
                if b"DONE" in b"".join(chunks):
                    break
            elif proc.poll() is not None:
                break
            else:
                time.sleep(0.05)
        else:
            print(f"(timed out after {_TIMEOUT_SEC}s)", flush=True)
    finally:
        proc.kill()
        proc.wait()
    out = b"".join(chunks).decode(errors="replace")

    print(out, end="", flush=True)
    if "DONE" not in out:
        print("FAILED: probe never reached its end", file=sys.stderr)
        return 1

    passed = "RESULT PASS" in out
    if args.expect_fail:
        if passed:
            print("FAILED: seeded mismatch was not detected", file=sys.stderr)
            return 1
        print("OK: seeded mismatch was detected and reported", flush=True)
        return 0
    if not passed:
        print("FAILED: probe reported failures", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
