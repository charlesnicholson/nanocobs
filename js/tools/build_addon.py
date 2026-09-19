#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense OR 0BSD
"""Compile the N-API addon into <pkg>/prebuilds/<target>/nanocobs.node.

One code path for local builds and for the release matrix, because the three platforms
need genuinely different link lines and encoding that in the Makefile meant the Windows
branch was never exercised until CI ran it.

The target name is what js/src/loader.js resolves at import time: platform-arch, plus
-glibc or -musl on Linux. It comes from the Node that will load the addon, not from the
Python running this, so cross-checking it is the caller's job.

On Windows this expects to run inside a developer command prompt (vcvarsall has been
called, cl.exe is on PATH); see the workflow for the vswhere incantation.

Standard library only, like release.py.
"""

import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = [ROOT / "js" / "native" / "cobs_napi.c", ROOT / "cobs.c"]

# Node reports the platform, arch and libc; nothing here should guess them.
# An expression, not a statement: `node -p` prints whatever it evaluates to, so a
# process.stdout.write() here would append its own "true" to the answer.
_PROBE = (
    '[process.platform, process.arch, process.versions.node, '
    '(process.platform === "linux" '
    '? (process.report.getReport().header.glibcVersionRuntime ? "glibc" : "musl") '
    ': "")].join(" ")'
)


def probe(node):
    out = subprocess.run([node, "-p", _PROBE], capture_output=True, text=True, check=True)
    fields = out.stdout.strip().split(" ")
    if len(fields) not in (3, 4):
        msg = f"unexpected probe output {out.stdout!r}"
        raise ValueError(msg)
    platform, arch, version = fields[:3]
    return platform, arch, version, (fields[3] if len(fields) == 4 else "")


def target_name(platform, arch, libc):
    return f"{platform}-{arch}" + (f"-{libc}" if libc else "")


def find_headers(version, explicit):
    """node-gyp's per-version cache, wherever this platform keeps it."""
    if explicit:
        return pathlib.Path(explicit)
    home = pathlib.Path.home()
    local = os.environ.get("LOCALAPPDATA")
    candidates = [
        home / "Library" / "Caches" / "node-gyp" / version / "include" / "node",
        home / ".cache" / "node-gyp" / version / "include" / "node",
        home / ".node-gyp" / version / "include" / "node",
    ]
    if local:
        candidates.append(pathlib.Path(local) / "node-gyp" / "Cache" / version /
                          "include" / "node")
    for c in candidates:
        if (c / "node_api.h").is_file():
            return c
    return None


def find_node_lib(version, arch):
    """Windows links against node.lib; node-gyp caches it beside the headers."""
    local = os.environ.get("LOCALAPPDATA")
    roots = [pathlib.Path(local) / "node-gyp" / "Cache"] if local else []
    roots.append(pathlib.Path.home() / ".node-gyp")
    for root in roots:
        lib = root / version / arch / "node.lib"
        if lib.is_file():
            return lib
    return None


def command(platform, out, headers, node_lib):
    inc = ["-I", str(headers), "-I", str(ROOT), "-I", str(ROOT / "js" / "native")]
    if platform == "win32":
        # /W3 rather than /W4 /WX: the C is already -Wall -Wextra -Werror clean under
        # clang and gcc, and node_api.h is not ours to keep warning-free.
        return [
            "cl", "/nologo", "/O2", "/DNDEBUG", "/W3", "/LD",
            f"/I{headers}", f"/I{ROOT}", f"/I{ROOT / 'js' / 'native'}",
            *[str(s) for s in SOURCES],
            f"/Fe:{out}", f"/Fo:{out.parent}{os.sep}",
            "/link", "/DLL", str(node_lib),
        ]
    # napi_* is resolved by the host process at load, not linked in.
    link = (["-bundle", "-undefined", "dynamic_lookup"] if platform == "darwin"
            else ["-shared"])
    cc = os.environ.get("CC", "cc")
    return [cc, "-O3", "-DNDEBUG", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fPIC",
            *link, *inc, "-o", str(out), *[str(s) for s in SOURCES]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--node", default="node", help="the Node that will load the addon")
    ap.add_argument("--pkg", default=str(ROOT / "build" / "js"),
                    help="package tree; the addon lands under its prebuilds/")
    ap.add_argument("--headers", help="override the node-gyp header directory")
    ap.add_argument("--expect-target", help="fail unless the host resolves to this")
    args = ap.parse_args()

    platform, arch, version, libc = probe(args.node)
    target = target_name(platform, arch, libc)
    if args.expect_target and (target != args.expect_target):
        print(f"host is {target}, expected {args.expect_target}", file=sys.stderr)
        return 1

    headers = find_headers(version, args.headers)
    if not headers:
        print(f"no Node headers for v{version}; run 'npx node-gyp install' or pass "
              f"--headers", file=sys.stderr)
        return 2

    node_lib = None
    if platform == "win32":
        node_lib = find_node_lib(version, arch)
        if not node_lib:
            print(f"no node.lib for v{version}/{arch}; run 'npx node-gyp install'",
                  file=sys.stderr)
            return 2

    # Absolute: the Windows branch runs cl.exe with cwd set to this directory, to keep
    # its .obj litter out of the tree, and a relative /Fo: would resolve against that
    # and double the path.
    out = (pathlib.Path(args.pkg) / "prebuilds" / target / "nanocobs.node").resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = command(platform, out, headers, node_lib)
    print("> " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=out.parent if platform == "win32" else None)
    if result.returncode:
        return result.returncode
    if not out.is_file():
        print(f"{out} was not produced", file=sys.stderr)
        return 1
    print(f"  {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
