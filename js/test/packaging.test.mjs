// SPDX-License-Identifier: Unlicense OR 0BSD
//
// What ends up in the tarball. Catches a shipped test directory or a missing wasm
// module on the PR rather than on tag day.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const pkgDir = new URL('..', import.meta.url).pathname;

test('npm pack ships exactly the intended files', () => {
  const out = execFileSync('npm', ['pack', '--dry-run', '--json'],
                           { cwd: pkgDir, encoding: 'utf8' });
  const all = JSON.parse(out)[0].files.map(f => f.path).sort();

  // Prebuilds vary by host and by what the release matrix produced, so they are
  // checked by shape. Everything else is pinned: a stray test directory or a missing
  // wasm module should fail on the PR, not on tag day.
  const prebuilds = all.filter(f => f.startsWith('prebuilds/'));
  for (const f of prebuilds) {
    assert.match(f, /^prebuilds\/[a-z0-9]+-[a-z0-9]+(-(glibc|musl))?\/nanocobs\.node$/,
                 `unexpected prebuild path ${f}`);
  }
  assert.deepEqual(all.filter(f => !f.startsWith('prebuilds/')), [
    'LICENSE',
    'README.md',
    'binding.gyp',
    'native/cobs.c',
    'native/cobs.h',
    'native/cobs_napi.c',
    'package.json',
    'src/base64.js',
    'src/index.d.ts',
    'src/index.js',
    'src/loader.js',
    'src/native.js',
    'src/node.js',
    'src/shared.js',
    'src/wasm.js',
  ]);
});

test('the manifest keeps its throwaway version until a release stamps it', () => {
  // release.py --check guards the source manifest's @COBS_VERSION@; this catches an
  // assembled tree carrying a stray real version instead of the dev stand-in.
  const pkg = JSON.parse(readFileSync(new URL('../package.json', import.meta.url)));
  assert.equal(pkg.version, '0.0.0-dev');
  assert.equal(pkg.name, 'nanocobs');
  assert.equal(pkg.type, 'module');
});

test('the portable entry point imports no Node built-ins', () => {
  // What browsers, bundlers, Deno and Workers resolve to must stay host-free: the wasm
  // is inlined so there is nothing to load. node.js and loader.js are exempt by
  // design -- only the "node" export condition reaches them.
  for (const f of ['index.js', 'base64.js', 'wasm.js', 'shared.js']) {
    const src = readFileSync(new URL(`../src/${f}`, import.meta.url), 'utf8');
    assert.equal(/from\s+['"]node:/.test(src), false, `${f} imports a node: built-in`);
    assert.equal(/require\s*\(/.test(src), false, `${f} uses require()`);
  }
});
