# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""Tests for lngpack.py (stdlib unittest). Run from the kit root: cd tools && python3 -m unittest test_lngpack

They use the kit's template, ../lang/template.json.
"""
import copy
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

import lngpack

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(os.path.dirname(HERE), 'lang', 'template.json')
with open(TEMPLATE, encoding='utf-8') as f:
  TPL = json.load(f)
K = 'Bad Argument Type'
OK = dict(code='es', name='Español')


def one(k, v, **o):
  return lngpack.build({k: v}, TPL, **(o or OK))


def refused(test, k, v, pattern, **o):
  with test.assertRaises(lngpack.LngError) as cm:
    one(k, v, **o)
  test.assertRegex(str(cm.exception), pattern)


class HpBytes(unittest.TestCase):
  def test_charset(self):
    cs = lngpack.Charset(TPL)
    self.assertEqual(cs.encode('A\u2192x\u0304\u00e9'), [0x41, 0x8d, 0x81, 0xe9])
    self.assertEqual(cs.encode('\u202f'), [0xa0])
    self.assertEqual(cs.decode([0x8d, 0x81]), '\u2192x\u0304')
    with self.assertRaisesRegex(lngpack.LngError, r'no calculator character for "ł" \(U\+0142\)'):
      cs.encode('Zły')


class Build(unittest.TestCase):
  def test_round_trip(self):
    r = lngpack.read_lng(one(K, 'Falscher Typ').data)
    self.assertEqual((r['code'], r['name'], r['version']), ('es', 'Español', 1))
    self.assertEqual(r['entries'][lngpack.fnv1a(list(K.encode('ascii')))], list(b'Falscher Typ'))

  def test_layout(self):
    d = lngpack.build({K: 'x', 'Too Few Arguments': 'y'}, TPL, **OK).data
    magic, version, catalogue, count, s_off, size = struct.unpack_from('<4sB3xIIII', d)
    self.assertEqual((magic, version, count, s_off, size), (b'RLNG', 1, 2, 48 + 16, len(d)))
    self.assertEqual(d[24:32], b'es' + b'\0' * 6)
    hashes = [struct.unpack_from('<I', d, 48 + 8 * i)[0] for i in range(2)]
    self.assertEqual(hashes, sorted(hashes))
    keys = sorted(k for k in TPL if not k.startswith('@'))
    self.assertEqual(catalogue, lngpack.fnv1a(lngpack.Charset(TPL).encode('\n'.join(keys))))

  def test_newline_0x1f_u202f(self):
    r = lngpack.read_lng(one(K, 'a\nb\u001f\u202fc').data)
    self.assertEqual(r['entries'][lngpack.fnv1a(list(K.encode()))], [0x61, 10, 0x62, 0x1f, 0xa0, 0x63])

  def test_refuses(self):
    bad = next(k for k in TPL if not k.startswith('@') and '{' in k and '}' in k)
    refused(self, bad, 'x', 'placeholder')
    import re
    refused(self, bad, re.sub(r'\{\w+\}', '{zz}', bad, count=1), 'placeholder')
    refused(self, K, 'Zły', 'ł')
    refused(self, K, 'a\u0001b', 'control character U\\+0001')
    refused(self, K, 'a' * 256, '255')
    refused(self, 'No such key at all', 'x', 'template')
    for c in ['e', 'abcd', 'E1', 'xx1', 'es\n']:
      refused(self, K, 'x', 'code', code=c, name='X')
    for c in ['en', 'de', 'fr']:
      refused(self, K, 'x', 'built-in', code=c, name='X')
    refused(self, K, 'x', 'name is 17 bytes', code='es', name='x' * 17)
    refused(self, K, 'x', 'name is empty', code='es', name='')
    for n in ['a\u0000b', 'a\u0001', 'a\nb', 'a\u001fb']:
      refused(self, K, 'x', 'name has a control byte', code='es', name=n)

  def test_keeps_device_text_and_spaces(self):
    k = next(k for k, b in TPL.items() if not k.startswith('@') and b.get('keep'))
    refused(self, k, 'x', 'device text "%s" missing' % TPL[k]['keep'][0])
    k = next(k for k, b in TPL.items() if not k.startswith('@') and b.get('names'))
    refused(self, k, 'x', 'RPL name "%s" missing' % TPL[k]['names'][0])
    k = next(k for k in TPL if not k.startswith('@') and k.endswith(' ') and not k.startswith(' '))
    refused(self, k, 'x', 'trailing space differs')
    refused(self, K, '   ', 'empty')

  def test_warns_width(self):
    k = next(k for k, b in TPL.items() if not k.startswith('@') and 'px' in b and k.strip() == k and '{' not in k and not b.get('names'))
    r = one(k, 'W' * 60)
    self.assertTrue(any(k in w and 'px wide' in w for w in r.warnings))
    self.assertEqual(len(lngpack.read_lng(r.data)['entries']), 1)

  def test_warns_cap(self):
    n = len(K)
    cap = max(2 * n, n + 16)
    longer = lambda v: [w for w in one(K, v).warnings if 'longer than the firmware shows' in w]
    self.assertEqual(longer('a' * cap), [])
    self.assertEqual(longer('a' * (cap + 1)), ['%s: longer than the firmware shows (max %d bytes); it will show English' % (json.dumps(K), cap)])

  def test_untranslated(self):
    c = copy.deepcopy(TPL)
    c[K] = 'Typ'
    r = lngpack.build(c, TPL, **OK)
    self.assertEqual(len(lngpack.read_lng(r.data)['entries']), 1)
    self.assertEqual(r.untranslated, sum(1 for k in TPL if not k.startswith('@')) - 1)

  def test_read_lng_refuses_malformed(self):
    good = one(K, 'x').data
    for b in [good[:20], good[:12] + b'\xff\xff\xff\x00' + good[16:], good[:-1] + b'A']:
      with self.assertRaises(lngpack.LngError):
        lngpack.read_lng(b)


class Cli(unittest.TestCase):
  def run_cli(self, *args):
    return subprocess.run([sys.executable, os.path.join(HERE, 'lngpack.py')] + list(args), capture_output=True, text=True)

  def test_build_and_dump(self):
    with tempfile.TemporaryDirectory() as d:
      src = os.path.join(d, 'es.json')
      with open(src, 'w', encoding='utf-8') as f:
        json.dump({'@name': 'Español', K: 'Tipo de argumento'}, f, ensure_ascii=False)
      r = self.run_cli(src)
      self.assertEqual(r.returncode, 0, r.stderr)
      self.assertTrue(os.path.exists(os.path.join(d, 'es.lng')))
      r = self.run_cli('--dump', os.path.join(d, 'es.lng'))
      self.assertEqual(r.returncode, 0, r.stderr)
      self.assertIn('code es', r.stdout)
      self.assertIn('Español', r.stdout)
      self.assertIn('%s -> %s' % (json.dumps(K), json.dumps('Tipo de argumento', ensure_ascii=False)), r.stdout)

  def test_refusal_exit_1(self):
    with tempfile.TemporaryDirectory() as d:
      src = os.path.join(d, 'de.json')
      with open(src, 'w', encoding='utf-8') as f:
        json.dump({'@name': 'Deutsch', K: 'Typ'}, f)
      r = self.run_cli(src)
      self.assertEqual(r.returncode, 1)
      self.assertIn('built-in', r.stderr)
      self.assertFalse(os.path.exists(os.path.join(d, 'de.lng')))


if __name__ == '__main__':
  unittest.main()
