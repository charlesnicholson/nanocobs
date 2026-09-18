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
  const files = JSON.parse(out)[0].files.map(f => f.path).sort();
  assert.deepEqual(files, [
    'LICENSE',
    'README.md',
    'package.json',
    'src/base64.js',
    'src/index.d.ts',
    'src/index.js',
    'src/wasm.js',
  ]);
});

test('the manifest keeps its placeholder version until a release stamps it', () => {
  // release.py --check does this for the source manifest; this catches an
  // assembled tree with a stray stamp.
  const pkg = JSON.parse(readFileSync(new URL('../package.json', import.meta.url)));
  assert.equal(pkg.version, '0.0.0');
  assert.equal(pkg.name, 'nanocobs');
  assert.equal(pkg.type, 'module');
});

test('the package imports no Node built-ins', () => {
  // The wasm is inlined so the package has no host dependencies. Keep it that way.
  for (const f of ['index.js', 'base64.js', 'wasm.js']) {
    const src = readFileSync(new URL(`../src/${f}`, import.meta.url), 'utf8');
    assert.equal(/from\s+['"]node:/.test(src), false, `${f} imports a node: built-in`);
    assert.equal(/require\s*\(/.test(src), false, `${f} uses require()`);
  }
});
