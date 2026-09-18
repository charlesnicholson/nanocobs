// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_paper_figures.cc.
// http://www.stuartcheshire.org/papers/COBSforToN.pdf

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { u8, runOf, concat } from './helpers.mjs';

test('COBS paper examples', async (t) => {
  await t.test('Figure 2', () => {
    const input = concat(runOf(679, 0x01), u8(0x00));
    const expected = concat(
      u8(0xFF), runOf(254, 0x01),
      u8(0xFF), runOf(254, 0x01),
      u8(0xAC), runOf(172, 0x01),
      u8(0x00));
    assert.deepEqual(cobs.encode(input), expected);
    assert.deepEqual(cobs.decode(expected), input);
  });

  await t.test('Figure 3', () => {
    const input = u8(0x45, 0x00, 0x00, 0x2C, 0x4C, 0x79,
                     0x00, 0x00, 0x40, 0x06, 0x4F, 0x37);
    const expected = u8(0x02, 0x45, 0x01, 0x04, 0x2C, 0x4C, 0x79,
                        0x01, 0x05, 0x40, 0x06, 0x4F, 0x37, 0x00);
    assert.deepEqual(cobs.encode(input), expected);
    assert.deepEqual(cobs.decode(expected), input);
  });
});
