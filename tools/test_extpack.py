# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""Tests for extpack.py (stdlib unittest). Run from the kit root: cd tools && python3 -m unittest test_extpack

The ELF tests build tiny extensions with arm-none-eabi-gcc and skip cleanly when it is not installed; the checked-in
fixture is checked without it, and only when run inside the DM48 firmware repository (otherwise that test is skipped).
"""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

import extpack

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(KIT))
FIXTURE = os.path.join(REPO, 'firmware', 'tests', 'unit', 'fixtures', 'popcnt.ext')
GCC = shutil.which('arm-none-eabi-gcc')
# The template's flags (template/Makefile).
CFLAGS = ['-mthumb', '-march=armv7e-m', '-mfloat-abi=hard', '-mfpu=fpv4-sp-d16', '-Os', '-ffunction-sections',
          '-fdata-sections', '-fno-common', '-nostdlib', '-ffreestanding', '-fno-builtin',
          '-fno-tree-loop-distribute-patterns', '-mword-relocations', '-I' + os.path.join(KIT, 'include')]
LDFLAGS = ['-T', os.path.join(KIT, 'ld', 'ext.ld'), '-Wl,-q', '-Wl,--unresolved-symbols=ignore-all']

TINY = r'''
#include "dm48ext.h"
extern int dx_frob2(int);             /* a hypothetical @2 export */
static int count = 3;
static int twice(void) {
  if (dx_frob2(count)) return -1;
  return dx_push(dx_make_real(2.0));
}
int dx_init(void) { return dx_register("TWICE", 1, twice); }
'''

# An initialised static the command writes, so it stays in .data (a constant one would be folded or put in .rodata).
DATA = r'''
#include "dm48ext.h"
unsigned stamp = 0x5A17C0DEu;
static int bump(void) { stamp++; return dx_push(dx_make_real((double)stamp)); }
int dx_init(void) { return dx_register("BUMP", 0, bump); }
'''

# No .data and no .bss: code and read-only data alone.
CODE_ONLY = r'''
#include "dm48ext.h"
static int one(void) { return dx_push(dx_make_real(1.0)); }
int dx_init(void) { return dx_register("ONE", 0, one); }
'''

# .bss without .data.
BSS_ONLY = r'''
#include "dm48ext.h"
char buf[10];
static int inc(void) { buf[1]++; return dx_push(dx_make_real(buf[1])); }
int dx_init(void) { return dx_register("INC", 0, inc); }
'''


def build(src, extra=(), ld=None):
  """Compile and link src as an extension (with ld, the text of another linker script); return the ELF bytes."""
  with tempfile.TemporaryDirectory() as d:
    c, elf = os.path.join(d, 'x.c'), os.path.join(d, 'x.elf')
    with open(c, 'w') as f:
      f.write(src)
    ldflags = LDFLAGS
    if ld is not None:
      with open(os.path.join(d, 'x.ld'), 'w') as f:
        f.write(ld)
      ldflags = ['-T', os.path.join(d, 'x.ld')] + LDFLAGS[2:]
    subprocess.run([GCC, *CFLAGS, *extra, c, *ldflags, '-lgcc', '-o', elf], check=True, capture_output=True)
    with open(elf, 'rb') as f:
      return f.read()


class Names(unittest.TestCase):
  def test_import_names(self):
    self.assertEqual(extpack.import_name('dx_push'), 'dx_push@1')
    self.assertEqual(extpack.import_name('dx_push2'), 'dx_push@2')
    self.assertEqual(extpack.import_name('dx_frob12'), 'dx_frob@12')
    self.assertEqual(extpack.import_name('dx_fw_version'), 'dx_fw_version@1')

  def test_extension_names(self):
    for good in ('popcnt', 'P', 'Bits64', 'A23456789012345'):
      self.assertIsNone(extpack.name_error(good), good)
    for bad in ('', '1abc', 'a-b', 'A234567890123456', 'é'):
      self.assertIsNotNone(extpack.name_error(bad), bad)


def check_file(t, data):
  """The structural invariants of any .ext file; returns the parsed form."""
  x = extpack.read_ext(data)
  h = x['header']
  t.assertEqual(data[:4], b'DMXT')
  # The brief said 84 bytes, but its field list (24 bytes of magic/format/flags/name + 16 u32) adds up to 88.
  t.assertEqual(extpack.HEADER_SIZE, 88)
  t.assertEqual(struct.calcsize(extpack.HEADER_FMT), 88)
  t.assertEqual(h['format'], 2)
  t.assertEqual(h['flags'], 0)
  t.assertEqual(h['code_size'] % 32, 0)
  t.assertLessEqual(h['code_size'], h['image_size'])
  t.assertEqual((h['sig_off'], h['sig_size']), (0, 0))
  t.assertEqual(h['crc32'], zlib.crc32(data[88:]) & 0xFFFFFFFF)
  t.assertEqual(len(x['image']), h['image_size'])
  t.assertTrue(h['init_off'] & 1, 'init_off carries the Thumb bit')
  t.assertLess(h['init_off'], h['image_size'])
  for off, typ, imp in x['relocs']:
    t.assertIn(typ, (1, 2, 3))
    t.assertLessEqual(off + 4, h['image_size'])
    if typ == 1:
      t.assertEqual(imp, 0xFFFF)
      word = struct.unpack_from('<I', x['image'], off)[0]
      t.assertLess(word, h['image_size'] + h['bss_size'] + 1)
    else:
      t.assertLess(imp, len(x['imports']))
    if typ == 3:
      t.assertLessEqual(off + 4, h['code_size'], 'a call to an import lies in the code part')
      hw1, hw2 = struct.unpack_from('<HH', x['image'], off)
      t.assertEqual(hw1 & 0xF800, 0xF000, 'BL/B.W first halfword')
      t.assertIn(hw2 & 0xD000, (0xD000, 0x9000), 'BL or B.W second halfword')
  return x


@unittest.skipUnless(GCC, 'arm-none-eabi-gcc not installed')
class Pack(unittest.TestCase):
  def test_header_and_crc(self):
    data = extpack.pack(build(TINY), name='tiny', version=7)
    x = check_file(self, data)
    h = x['header']
    self.assertEqual(h['name'], 'tiny')
    self.assertEqual(h['version'], 7)
    self.assertEqual(data[8:24], b'tiny' + bytes(12))
    self.assertEqual(h['wasm_size'], 0)
    self.assertGreater(h['bss_size'] + h['image_size'], 0)
    self.assertEqual(h['import_off'], 88 + ((h['image_size'] + 3) & ~3))
    self.assertEqual(h['reloc_off'], h['import_off'] + 4 * h['nimport'])
    self.assertEqual(h['strtab_off'], h['reloc_off'] + 8 * h['nreloc'])
    self.assertEqual(len(data), h['strtab_off'] + h['strtab_size'])

  def test_imports_are_the_dx_symbols_used(self):
    x = check_file(self, extpack.pack(build(TINY), name='tiny'))
    self.assertEqual(sorted(x['imports']), ['dx_frob@2', 'dx_make_real@1', 'dx_push@1', 'dx_register@1'])
    used = {x['imports'][imp] for _, typ, imp in x['relocs'] if typ in (2, 3)}
    self.assertEqual(used, set(x['imports']))

  def test_function_pointer_import_is_abs32_import(self):
    src = '#include "dm48ext.h"\nint (*volatile p)(dx_obj) = dx_push;\nint dx_init(void) { return p(0); }\n'
    x = check_file(self, extpack.pack(build(src), name='fp'))
    self.assertIn((2, 'dx_push@1'), {(typ, x['imports'][imp]) for _, typ, imp in x['relocs'] if typ != 1})

  def test_wasm_section(self):
    data = extpack.pack(build(TINY), name='tiny', wasm=b'\0asm\1\0\0\0')
    x = check_file(self, data)
    h = x['header']
    self.assertEqual(h['wasm_size'], 8)
    self.assertEqual(h['wasm_off'] % 4, 0)
    self.assertEqual(data[h['wasm_off']:h['wasm_off'] + 8], b'\0asm\1\0\0\0')

  def test_memcpy_is_a_build_error(self):
    src = ('#include "dm48ext.h"\nvoid *memcpy(void *, const void *, unsigned);\nstatic char a[8], b[8];\n'
           'int dx_init(void) { memcpy(a, b, 8); return 0; }\n')
    with self.assertRaises(extpack.PackError) as cm:
      extpack.pack(build(src), name='bad')
    self.assertIn('memcpy', str(cm.exception))

  def test_unsupported_relocation_is_a_build_error(self):
    src = ('#include "dm48ext.h"\nint v;\n'
           'int dx_init(void) { int r; __asm__("movw %0, #:lower16:v" : "=r"(r)); return r; }\n')
    with self.assertRaises(extpack.PackError) as cm:
      extpack.pack(build(src), name='bad')
    self.assertIn('R_ARM_THM_MOVW_ABS_NC', str(cm.exception))
    self.assertIn('v', str(cm.exception))

  def test_weak_import_is_a_build_error(self):
    src = ('#include "dm48ext.h"\nextern int dx_frob(void) __attribute__((weak));\n'
           'int dx_init(void) { return dx_frob(); }\n')
    with self.assertRaises(extpack.PackError) as cm:
      extpack.pack(build(src), name='bad')
    self.assertIn('dx_frob', str(cm.exception))
    self.assertIn('weak', str(cm.exception))

  def test_call_place_must_be_a_branch(self):
    elf = bytearray(build(TINY))
    e = extpack.Elf(bytes(elf))
    text = next(s for s in e.sections if s['name'] == '.text')
    rel = next(s for s in e.sections if s['name'] == '.rel.text')
    for k in range(rel['size'] // 8):
      off, info = struct.unpack_from('<II', elf, rel['offset'] + 8 * k)
      if info & 0xFF == 10 and e.symbols[info >> 8]['name'] == 'dx_push':
        break
    struct.pack_into('<HH', elf, text['offset'] + off, 0xF3AF, 0x8000)   # nop.w
    with self.assertRaises(extpack.PackError) as cm:
      extpack.pack(bytes(elf), name='bad')
    self.assertIn('0x%x' % off, str(cm.exception))
    self.assertIn('BL', str(cm.exception))

  def test_v2_header(self):
    h = check_file(self, extpack.pack(build(TINY), name='tiny'))['header']
    self.assertEqual(h['format'], 2)
    self.assertEqual(h['code_size'] % 32, 0)
    self.assertLessEqual(h['code_size'], h['image_size'])
    self.assertNotIn('ncmd', extpack.HEADER_FIELDS)
    self.assertEqual(extpack.HEADER_FIELDS.index('code_size'), extpack.HEADER_FIELDS.index('init_off') + 1)
    self.assertEqual(extpack.MAX_ALIGN, 32)

  def test_data_after_code(self):
    elf = build(DATA)
    data = extpack.pack(elf, name='data')
    x = check_file(self, data)
    h = x['header']
    self.assertGreater(h['image_size'], h['code_size'])
    self.assertEqual(struct.unpack_from('<I', x['image'], h['code_size'])[0], 0x5A17C0DE)
    stamp = next(s for s in extpack.Elf(elf).symbols if s['name'] == 'stamp')
    self.assertEqual(stamp['value'], h['code_size'])
    self.assertLess(h['init_off'], h['code_size'])

  def test_no_data(self):
    h = check_file(self, extpack.pack(build(CODE_ONLY), name='code'))['header']
    self.assertEqual(h['code_size'], h['image_size'])
    self.assertEqual(h['image_size'] % 32, 0)
    self.assertEqual(h['bss_size'], 0)
    # .bss without .data: the code part padded, .bss after it
    elf = build(BSS_ONLY)
    h = check_file(self, extpack.pack(elf, name='bss'))['header']
    self.assertEqual(h['code_size'], h['image_size'])
    self.assertEqual(h['image_size'] % 32, 0)
    self.assertGreaterEqual(h['bss_size'], 10)
    buf = next(s for s in extpack.Elf(elf).symbols if s['name'] == 'buf')
    self.assertGreaterEqual(buf['value'], h['code_size'])

  def test_call_reloc_in_code(self):
    for src in (TINY, DATA, CODE_ONLY):
      x = check_file(self, extpack.pack(build(src), name='x'))
      calls = [off for off, typ, _ in x['relocs'] if typ == 3]
      self.assertTrue(calls)
      self.assertTrue(all(off < x['header']['code_size'] for off in calls))

  def test_call_in_data_is_a_build_error(self):
    # a function put in .data by attribute: the data part would hold code (and its call to an import)
    src = ('#include "dm48ext.h"\n__attribute__((section(".data.f"), noinline)) int f(void) { return dx_drop(1); }\n'
           'int dx_init(void) { return f(); }\n')
    with self.assertRaises(extpack.PackError) as cm:
      extpack.pack(build(src), name='bad')
    self.assertIn('data part', str(cm.exception))
    # the relocation check itself: a call to an import past code_size
    elf = extpack.Elf(build(TINY))
    image, code_size, bss = extpack.layout(elf)
    calls = [off for off, typ, _ in extpack.relocations(elf, image, code_size, bss) if typ == 3]
    with self.assertRaises(extpack.PackError) as cm:
      extpack.relocations(elf, image, max(calls), bss)
    self.assertIn('outside the code part', str(cm.exception))

  def test_custom_writable_section_is_data(self):
    # no .data, but a writable variable in a section of its own: it starts the data part, not the code part
    src = ('#include "dm48ext.h"\n__attribute__((section(".mydata"))) unsigned mine = 0x600DF00Du;\n'
           'static int bump(void) { mine++; return dx_push(dx_make_real((double)mine)); }\n'
           'int dx_init(void) { return dx_register("MINE", 0, bump); }\n')
    elf = build(src)
    x = check_file(self, extpack.pack(elf, name='mine'))
    h = x['header']
    e = extpack.Elf(elf)
    mine = next(s for s in e.symbols if s['name'] == 'mine')
    self.assertFalse(any(s['name'] == '.data' and s['size'] for s in e.sections))
    self.assertGreaterEqual(mine['value'], h['code_size'])
    self.assertLess(h['code_size'], h['image_size'])
    self.assertEqual(struct.unpack_from('<I', x['image'], mine['value'])[0], 0x600DF00D)

  def test_unaligned_data_is_a_build_error(self):
    with open(os.path.join(KIT, 'ld', 'ext.ld')) as f:
      ld = f.read().replace('. = ALIGN(32);', '').replace('.data : ALIGN(32)', '.data : ALIGN(8)')
    # a 4-byte .rodata after the code moves .data off any 32-byte line
    src = DATA.replace('unsigned stamp', 'const unsigned pad[1] = {7};\nunsigned stamp')
    for extra in range(0, 32, 4):
      body = src.replace('{7}', '{%s}' % ', '.join(['7'] * (1 + extra // 4))).replace('pad[1]', 'pad[%d]' % (1 + extra // 4))
      elf = extpack.Elf(build(body, ld=ld))
      data = next(s for s in elf.sections if s['name'] == '.data')
      if data['addr'] % 32:
        break
    else:
      self.fail('could not place .data off a 32-byte line')
    with self.assertRaises(extpack.PackError) as cm:
      extpack.layout(elf)
    self.assertIn('not on a 32-byte boundary', str(cm.exception))
    self.assertIn('.data', str(cm.exception))

  def test_bad_name_is_an_error(self):
    with self.assertRaises(extpack.PackError):
      extpack.pack(build(TINY), name='no-dash')

  def test_cli(self):
    with tempfile.TemporaryDirectory() as d:
      elf, out = os.path.join(d, 'tiny.elf'), os.path.join(d, 'tiny.ext')
      with open(elf, 'wb') as f:
        f.write(build(TINY))
      r = subprocess.run(['python3', os.path.join(HERE, 'extpack.py'), elf, '-o', out, '--version', '3'],
                         capture_output=True, text=True)
      self.assertEqual(r.returncode, 0, r.stderr)
      with open(out, 'rb') as f:
        h = check_file(self, f.read())['header']
      self.assertEqual((h['name'], h['version']), ('tiny', 3))


class Fixture(unittest.TestCase):
  @unittest.skipUnless(os.path.exists(FIXTURE), 'the checked-in fixture lives in the firmware repository only')
  def test_checked_in_fixture(self):
    with open(FIXTURE, 'rb') as f:
      x = check_file(self, f.read())
    self.assertEqual(x['header']['name'], 'popcnt')
    self.assertIn('dx_register@1', x['imports'])
    self.assertTrue(all(n.startswith('dx_') and '@' in n for n in x['imports']))


if __name__ == '__main__':
  unittest.main()
