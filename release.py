"""Prepare the assets for a nanocobs release. Publishing them is the caller's job.

cobs.h carries a @COBS_VERSION@ placeholder and js/package.json a 0.0.0 one, so nothing
checked in can go stale. This stamps a tag into them and writes, into --out: a zip of
cobs.c and the stamped cobs.h, plus release_notes.txt.

Standard library only. The release job needs a wasm toolchain for the npm package, not
for this script.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import zipfile

_SCRIPT_PATH = pathlib.Path(__file__).resolve().parent
_HEADER = "cobs.h"
_SOURCE = "cobs.c"
_PLACEHOLDER = "@COBS_VERSION@"
_NPM_MANIFEST = pathlib.Path("js") / "package.json"
_NPM_PLACEHOLDER_VERSION = "0.0.0"

# A tag like v1.2.3-rc.1 must not become `latest` on npm.
_SEMVER = re.compile(
    r"^(?P<core>[0-9]+\.[0-9]+\.[0-9]+)(?:-(?P<pre>[0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$"
)


def _parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", help="Version tag to stamp into the header, e.g. v0.3.0")
    parser.add_argument("--repo", help="Repository to link and query, e.g. owner/nanocobs")
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("build/release"),
        help="Directory to write the assets into",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify the version placeholders are still unstamped in-tree, then exit",
    )
    parser.add_argument(
        "--stamp-npm",
        type=pathlib.Path,
        metavar="PKG_DIR",
        help="Stamp --tag into an assembled package.json in place and print the npm "
        "dist-tag. Point at build/js, never js/: the source manifest keeps its "
        "placeholder.",
    )
    parser.add_argument(
        "--npm-name",
        help="Also rewrite the package name; GitHub Packages only accepts @owner/name",
    )
    return parser.parse_args()


def _git(*args: str) -> str:
    """Run a git command in the repository and return its stdout."""
    return subprocess.run(
        ["git", *args], check=True, capture_output=True, text=True, cwd=_SCRIPT_PATH
    ).stdout


def _stamped_header(tag: str) -> bytes:
    """Return the header's bytes with its version placeholder replaced by the tag."""
    placeholder = _PLACEHOLDER.encode()
    data = (_SCRIPT_PATH / _HEADER).read_bytes()
    if placeholder not in data:
        msg = f"{_HEADER} has no {_PLACEHOLDER} placeholder to stamp"
        raise ValueError(msg)
    return data.replace(placeholder, tag.encode())


def _npm_version(tag: str) -> tuple[str, str]:
    """Map a git tag to an npm version and the dist-tag it should publish under."""
    version = tag[1:] if tag.startswith("v") else tag
    match = _SEMVER.match(version)
    if not match:
        msg = f"tag {tag!r} does not yield an npm-legal semver version"
        raise ValueError(msg)
    # A prerelease publishes to `next`, so it can never land on `latest`.
    return version, "next" if match.group("pre") else "latest"


def _check_npm_placeholder() -> None:
    """Raise unless the source manifest still holds its placeholder version.

    The counterpart of the @COBS_VERSION@ guard: only a release assigns a version.
    """
    manifest = json.loads((_SCRIPT_PATH / _NPM_MANIFEST).read_text(encoding="utf-8"))
    if manifest.get("version") != _NPM_PLACEHOLDER_VERSION:
        msg = (
            f"{_NPM_MANIFEST} version is {manifest.get('version')!r}, expected the "
            f"placeholder {_NPM_PLACEHOLDER_VERSION!r}; releases stamp it, commits do not"
        )
        raise ValueError(msg)


def _stamp_npm(pkg_dir: pathlib.Path, tag: str, name: str | None) -> str:
    """Stamp the version, and optionally the name, into an assembled package.json."""
    version, dist_tag = _npm_version(tag)
    path = pkg_dir / "package.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    # Idempotent per version, so a second call can rename for a scoped registry
    # without rebuilding. Any other version means a different release stamped this tree.
    if manifest.get("version") not in (_NPM_PLACEHOLDER_VERSION, version):
        msg = (
            f"{path} already holds version {manifest.get('version')!r}, not the "
            f"placeholder or {version!r}"
        )
        raise ValueError(msg)
    manifest["version"] = version
    if name:
        manifest["name"] = name
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{path}: {manifest['name']}@{version} (dist-tag {dist_tag})", file=sys.stderr)
    return dist_tag


def _write_bundle(zip_path: pathlib.Path, prefix: str, header: bytes) -> None:
    """Zip the two files a user drops into their project, header already stamped."""
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr(f"{prefix}/{_HEADER}", header)
        bundle.writestr(f"{prefix}/{_SOURCE}", (_SCRIPT_PATH / _SOURCE).read_bytes())


def _pr_title(repo: str, pr: str) -> str:
    """Look up a pull request title, empty if the number names no pull request.

    A number in a commit message can name an issue instead, which 404s. Every other
    failure -- a token without pull-requests read, a rate limit -- would quietly drop
    entries from the notes, so raise on those rather than publish a short changelog.
    """
    result = subprocess.run(
        ["gh", "api", f"repos/{repo}/pulls/{pr}", "--jq", ".title"],
        check=False,
        capture_output=True,
        text=True,
        cwd=_SCRIPT_PATH,
    )
    if result.returncode == 0:
        return result.stdout.strip()
    if "HTTP 404" in result.stderr:
        return ""
    msg = f"reading the title of pull request {pr} failed: {result.stderr.strip()}"
    raise RuntimeError(msg)


def _release_notes(tag: str, repo: str, zip_name: str) -> str:
    """Compose the notes: pull requests landed since the previous tag, then the assets."""
    lines = [f"nanocobs {tag}:", ""]

    prev = [
        t
        for t in _git("tag", "--sort=-v:refname").split()
        if re.match(r"v[0-9]", t) and t != tag
    ]
    if prev:
        # A dry run names a tag that does not exist yet; walk to HEAD for it instead.
        exists = subprocess.run(
            ["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}"],
            check=False,
            capture_output=True,
            cwd=_SCRIPT_PATH,
        )
        log = _git("log", f"{prev[0]}..{tag if exists.returncode == 0 else 'HEAD'}", "--oneline")
        for pr in sorted(set(re.findall(r"\(#([0-9]+)\)", log)), key=int):
            title = _pr_title(repo, pr)
            if title:
                lines.append(f"{title} https://github.com/{repo}/pull/{pr}")
        lines.append("")

    version, dist_tag = _npm_version(tag)
    npm_install = f"npm install nanocobs@{version}"
    if dist_tag == "next":
        npm_install += "  (prerelease: published under the `next` dist-tag)"

    lines += [
        f"`{zip_name}`: `{_SOURCE}` and `{_HEADER}`, the header stamped `{tag}`.",
        "",
        f"JavaScript/TypeScript: `{npm_install}`",
        "",
        "The autogenerated source archives below carry an unstamped header, prefer the asset above.",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    """Stamp the header, then write the assets and the notes into the output directory."""
    args = _parse_args()

    if args.check:
        _stamped_header("checked")  # raises if the placeholder is gone
        _check_npm_placeholder()
        return 0

    if args.stamp_npm:
        if not args.tag:
            print("--stamp-npm requires --tag")
            return 1
        # stdout is the dist-tag alone, so the workflow can capture it directly.
        print(_stamp_npm(args.stamp_npm, args.tag, args.npm_name))
        return 0

    if not args.tag or not args.repo:
        print("--tag and --repo are required")
        return 1

    out = args.out if args.out.is_absolute() else _SCRIPT_PATH / args.out
    out.mkdir(parents=True, exist_ok=True)
    zip_name = f"nanocobs-{args.tag}.zip"

    _write_bundle(out / zip_name, f"nanocobs-{args.tag}", _stamped_header(args.tag))
    notes = out / "release_notes.txt"
    notes.write_text(_release_notes(args.tag, args.repo, zip_name), encoding="utf-8")
    for asset in (out / zip_name, notes):
        print(asset)

    return 0


if __name__ == "__main__":
    sys.exit(main())
