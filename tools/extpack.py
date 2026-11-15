#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""extpack - turn an arm-none-eabi-gcc ELF into a DM48 native extension (.ext, format 2).

  python3 extpack.py EXT.elf -o NAME.ext [--name NAME] [--version V] [--wasm FILE]
  python3 extpack.py --dump NAME.ext

The ELF must be linked with the kit's ld/ext.ld (one image from address 0, .bss last) and -Wl,-q (keep the
relocations). The format is described in ../README.md ("The .ext format"); HEADER_FMT below is its single
definition on this side. Python 3 standard library only.
"""
import argparse
import os
import re
import struct
import sys
import zlib

FORMAT = 2
MAGIC = b'DMXT'
# magic, format, flags, name, then 16 u32: version, image_size, bss_size, init_off, code_size, nimport, import_off,
# nreloc, reloc_off, strtab_off, strtab_size, wasm_off, wasm_size, sig_off, sig_size, crc32.
HEADER_FMT = '<4sHH16s16I'
HEADER_FIELDS = ('magic', 'format', 'flags', 'name', 'version', 'image_size', 'bss_size', 'init_off', 'code_size',
                 'nimport', 'import_off', 'nreloc', 'reloc_off', 'strtab_off', 'strtab_size', 'wasm_off', 'wasm_size',
                 'sig_off', 'sig_size', 'crc32')
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 88
RELOC_FMT = '<IBBH'                         # offset in the image, type, 0, import index (NO_IMPORT when none)
NO_IMPORT = 0xFFFF
ABS32, ABS32_IMPORT, THM_CALL_IMPORT = 1, 2, 3
MAX_ALIGN = 32                              # the loader places the image at a 32-aligned address
CODE_ALIGN = 32                             # code_size: the MPU's 32-byte lines

# ELF constants
SHT_SYMTAB, SHT_NOBITS, SHT_REL, SHT_RELA = 2, 8, 9, 4
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
STB_WEAK = 2
EF_ARM_ABI_FLOAT_HARD = 0x400
R_ARM_NONE, R_ARM_ABS32, R_ARM_THM_CALL, R_ARM_THM_JUMP24 = 0, 2, 10, 30
R_NAMES = {
  0: 'R_ARM_NONE', 1: 'R_ARM_PC24', 2: 'R_ARM_ABS32', 3: 'R_ARM_REL32', 10: 'R_ARM_THM_CALL', 11: 'R_ARM_THM_PC8',
  25: 'R_ARM_BASE_PREL', 26: 'R_ARM_GOT_BREL', 28: 'R_ARM_CALL', 29: 'R_ARM_JUMP24', 30: 'R_ARM_THM_JUMP24',
  38: 'R_ARM_TARGET1', 41: 'R_ARM_TARGET2', 42: 'R_ARM_PREL31', 43: 'R_ARM_MOVW_ABS_NC', 44: 'R_ARM_MOVT_ABS',
  47: 'R_ARM_THM_MOVW_ABS_NC', 48: 'R_ARM_THM_MOVT_ABS', 49: 'R_ARM_THM_MOVW_PREL_NC', 50: 'R_ARM_THM_MOVT_PREL',
  51: 'R_ARM_THM_JUMP19', 52: 'R_ARM_THM_JUMP6', 53: 'R_ARM_THM_ALU_PREL_11_0', 54: 'R_ARM_THM_PC12',
  96: 'R_ARM_GOT_PREL', 102: 'R_ARM_THM_JUMP11', 103: 'R_ARM_THM_JUMP8',
}
# PC-relative relocations between two places of the image: the linker has resolved them and they stay right
# wherever the image is loaded, so they are dropped.
PC_RELATIVE = {3, 10, 11, 30, 42, 51, 52, 53, 54, 102, 103}
NAME_RE = re.compile(r'[A-Za-z][A-Za-z0-9]{0,14}\Z')   # the firmware's rule for extension and command names

class PackError(Exception):
  pass

def rname(t):
  return R_NAMES.get(t, 'relocation type %d' % t)

def import_name(sym):
  """C symbol dx_fooN -> import dx_foo@N; a bare name is @1."""
  m = re.match(r'(.*?)(\d+)\Z', sym)
  return '%s@%d' % (m.group(1), int(m.group(2))) if m else sym + '@1'

def name_error(name):
  """None when name is a valid extension name, else why not."""
  if not NAME_RE.match(name):
    return 'extension name %r must be 1-15 letters and digits, starting with a letter' % name
  return None

class Elf:
  def __init__(self, data):
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
      raise PackError('not a 32-bit little-endian ELF file')
    (e_type, e_machine, _, _, _, e_shoff, e_flags, _, _, _, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    if e_machine != 40:
      raise PackError('not an ARM ELF file')
    if e_type != 2:
      raise PackError('not a linked executable (link with the kit\'s ld/ext.ld and -Wl,-q)')
    if not e_flags & EF_ARM_ABI_FLOAT_HARD:
      raise PackError('not built for the hard-float ABI (use -mfloat-abi=hard -mfpu=fpv4-sp-d16)')
    self.data = data
    self.sections = []
    for i in range(e_shnum):
      f = struct.unpack_from('<IIIIIIIIII', data, e_shoff + i * e_shentsize)
      self.sections.append(dict(zip(('name', 'type', 'flags', 'addr', 'offset', 'size', 'link', 'info',
                                     'addralign', 'entsize'), f)))
    shstr = self.sections[e_shstrndx]
    for s in self.sections:
      s['name'] = self.cstr(shstr['offset'] + s['name'])
    self.symbols = []
    for s in self.sections:
      if s['type'] == SHT_SYMTAB:
        strtab = self.sections[s['link']]['offset']
        for k in range(s['size'] // 16):
          st_name, value, size, info, _, shndx = struct.unpack_from('<IIIBBH', data, s['offset'] + 16 * k)
          self.symbols.append({'name': self.cstr(strtab + st_name), 'value': value, 'shndx': shndx,
                               'bind': info >> 4, 'type': info & 15})
    if not self.symbols:
      raise PackError('the ELF has no symbol table (do not strip it)')

  def cstr(self, off):
    return self.data[off:self.data.index(b'\0', off)].decode('latin-1')

  def section_bytes(self, s):
    return self.data[s['offset']:s['offset'] + s['size']]

def layout(elf):
  """The image bytes (PROGBITS from 0), the code part's size and the .bss size after the image.

  The code part is everything before the first writable section (code and read-only data); that section, .data
  with ld/ext.ld, starts the data part on a CODE_ALIGN boundary. Without a writable section the image is padded to
  CODE_ALIGN and is all code part."""
  alloc = [s for s in elf.sections if s['flags'] & SHF_ALLOC and s['size']]
  for s in alloc:
    if s['addralign'] > MAX_ALIGN:
      raise PackError('section %s needs %d-byte alignment; at most %d is supported'
                      % (s['name'], s['addralign'], MAX_ALIGN))
  progbits = [s for s in alloc if s['type'] != SHT_NOBITS]
  nobits = [s for s in alloc if s['type'] == SHT_NOBITS]
  if not progbits or min(s['addr'] for s in progbits) != 0:
    raise PackError('the image does not start at address 0 (link with the kit\'s ld/ext.ld)')
  image_size = max(s['addr'] + s['size'] for s in progbits)
  # The data part starts at the first writable section (.data or any other, by its SHF_WRITE flag): everything
  # before it is code and read-only data, everything from it on is writable and holds no code.
  writable = sorted((s for s in progbits if s['flags'] & SHF_WRITE), key=lambda s: s['addr'])
  if writable:
    first = writable[0]
    code_size = first['addr']
    if code_size % CODE_ALIGN:
      raise PackError('the data part (section %s) starts at 0x%x, not on a %d-byte boundary (use ld/ext.ld)'
                      % (first['name'], code_size, CODE_ALIGN))
    for s in progbits:
      if s['addr'] < code_size:
        continue
      if s['flags'] & SHF_EXECINSTR:
        raise PackError('section %s holds code but lies in the data part (from %s at 0x%x)'
                        % (s['name'], first['name'], code_size))
      if not s['flags'] & SHF_WRITE:
        raise PackError('section %s is read-only but lies in the data part (from %s at 0x%x): code, read-only '
                        'data and writable data must not interleave (use ld/ext.ld)' % (s['name'], first['name'], code_size))
  else:
    code_size = image_size = (image_size + CODE_ALIGN - 1) & ~(CODE_ALIGN - 1)
  image = bytearray(image_size)
  for s in progbits:
    image[s['addr']:s['addr'] + s['size']] = elf.section_bytes(s)
  for s in nobits:
    if s['addr'] < image_size:
      raise PackError('section %s (NOBITS) is not after the image (.bss must be last; use ld/ext.ld)' % s['name'])
  bss_end = max([s['addr'] + s['size'] for s in nobits] or [image_size])
  return image, code_size, bss_end - image_size

def relocations(elf, image, code_size, bss_size):
  """The loader's relocations [(offset, type, import name or None)], sorted by offset. A call to an import
  (THM_CALL_IMPORT) must lie in the code part, [0, code_size)."""
  image_size = len(image)
  out, errors, seen = [], [], False
  for rs in elf.sections:
    if rs['type'] == SHT_RELA:
      raise PackError('RELA relocations in %s are not supported' % rs['name'])
    if rs['type'] != SHT_REL:
      continue
    target = elf.sections[rs['info']]
    if not target['flags'] & SHF_ALLOC:
      continue                                    # debug information
    if target['type'] == SHT_NOBITS:
      raise PackError('relocations against %s (NOBITS)' % target['name'])
    seen = True
    for k in range(rs['size'] // 8):
      off, info = struct.unpack_from('<II', elf.data, rs['offset'] + 8 * k)
      typ, sym = info & 0xFF, elf.symbols[info >> 8]
      name = sym['name']
      if not name and sym['shndx'] < len(elf.sections):
        name = elf.sections[sym['shndx']]['name']    # a section symbol
      if typ == R_ARM_NONE:
        continue
      if off + 4 > image_size:
        raise PackError('%s against %s at 0x%x is outside the image' % (rname(typ), name, off))
      if sym['shndx'] == SHN_UNDEF:
        if not sym['name'].startswith('dx_'):
          errors.append('undefined symbol %s (%s at 0x%x): an extension can only call dx_* functions; '
                        'there is no libc' % (sym['name'], rname(typ), off))
        elif sym['bind'] == STB_WEAK:
          # ld resolves a weak undefined symbol to 0 (a BL becomes a NOP): the loader could not patch it.
          errors.append('weak undefined symbol %s (%s at 0x%x): declare dx_* functions without weak'
                        % (sym['name'], rname(typ), off))
        elif typ == R_ARM_ABS32:
          if struct.unpack_from('<I', image, off)[0]:
            errors.append('%s against %s+addend at 0x%x is not supported' % (rname(typ), sym['name'], off))
          out.append((off, ABS32_IMPORT, import_name(sym['name'])))
        elif typ in (R_ARM_THM_CALL, R_ARM_THM_JUMP24):
          hw1, hw2 = struct.unpack_from('<HH', image, off)
          if hw1 & 0xF800 != 0xF000 or hw2 & 0xD000 not in (0xD000, 0x9000):
            errors.append('%s against %s at 0x%x: the place is not a BL or B.W (%04x %04x)'
                          % (rname(typ), sym['name'], off, hw1, hw2))
          elif off + 4 > code_size:
            errors.append('%s against %s at 0x%x is outside the code part (0x%x bytes): code must not be in .data'
                          % (rname(typ), sym['name'], off, code_size))
          else:
            out.append((off, THM_CALL_IMPORT, import_name(sym['name'])))
        else:
          errors.append('unsupported relocation %s against %s at 0x%x' % (rname(typ), sym['name'], off))
      elif sym['shndx'] == SHN_ABS:
        errors.append('unsupported relocation %s against the absolute symbol %s at 0x%x' % (rname(typ), name, off))
      elif typ == R_ARM_ABS32:
        word = struct.unpack_from('<I', image, off)[0]
        if word > image_size + bss_size:
          errors.append('%s against %s at 0x%x points outside the image (0x%x)' % (rname(typ), name, off, word))
        out.append((off, ABS32, None))
      elif typ in PC_RELATIVE:
        continue
      else:
        errors.append('unsupported relocation %s against %s at 0x%x' % (rname(typ), name, off))
  if not seen:
    raise PackError('the ELF has no relocations (link with -Wl,-q)')
  if errors:
    raise PackError('\n'.join(errors))
  return sorted(out)

def align4(n):
  return (n + 3) & ~3

def pack(elf_bytes, name, version=1, wasm=None):
  """The .ext file for a linked extension ELF (bytes)."""
  err = name_error(name)
  if err:
    raise PackError(err)
  if not 0 <= version <= 0xFFFFFFFF:
    raise PackError('version must be a 32-bit unsigned number')
  elf = Elf(elf_bytes)
  image, code_size, bss_size = layout(elf)
  relocs = relocations(elf, image, code_size, bss_size)
  init = [s for s in elf.symbols if s['name'] == 'dx_init' and s['shndx'] not in (SHN_UNDEF, SHN_ABS)]
  if not init:
    raise PackError('dx_init is not defined')
  init_off = init[0]['value']
  if not init_off & 1 or init_off >= code_size:
    raise PackError('dx_init is not Thumb code in the code part of the image')

  imports = sorted({n for _, _, n in relocs if n})
  if len(imports) >= NO_IMPORT:
    raise PackError('too many imports')
  index = {n: i for i, n in enumerate(imports)}
  strtab, str_off = bytearray(), {}
  for n in imports:
    str_off[n] = len(strtab)
    strtab += n.encode('ascii') + b'\0'

  image_off = HEADER_SIZE
  import_off = image_off + align4(len(image))
  reloc_off = import_off + 4 * len(imports)
  strtab_off = reloc_off + 8 * len(relocs)
  wasm = wasm or b''
  wasm_off = align4(strtab_off + len(strtab)) if wasm else 0

  body = bytearray(image) + bytes(align4(len(image)) - len(image))
  for n in imports:
    body += struct.pack('<I', strtab_off + str_off[n])
  for off, typ, n in relocs:
    body += struct.pack(RELOC_FMT, off, typ, 0, index[n] if n else NO_IMPORT)
  body += strtab
  if wasm:
    body += bytes(wasm_off - HEADER_SIZE - len(body)) + wasm

  fields = dict(magic=MAGIC, format=FORMAT, flags=0, name=name.encode('ascii'), version=version,
                image_size=len(image), bss_size=bss_size, init_off=init_off, code_size=code_size, nimport=len(imports),
                import_off=import_off, nreloc=len(relocs), reloc_off=reloc_off, strtab_off=strtab_off,
                strtab_size=len(strtab), wasm_off=wasm_off, wasm_size=len(wasm), sig_off=0, sig_size=0,
                crc32=zlib.crc32(bytes(body)) & 0xFFFFFFFF)
  header = struct.pack(HEADER_FMT, *(fields[f] for f in HEADER_FIELDS))
  return header + bytes(body)

def read_ext(data):
  """Parse a .ext file (no validation beyond the structure): header dict, image, imports, relocs."""
  if len(data) < HEADER_SIZE or data[:4] != MAGIC:
    raise PackError('not a .ext file')
  h = dict(zip(HEADER_FIELDS, struct.unpack_from(HEADER_FMT, data)))
  h['name'] = h['name'].rstrip(b'\0').decode('ascii')
  image = data[HEADER_SIZE:HEADER_SIZE + h['image_size']]

  def cstr(off):
    return data[off:data.index(b'\0', off)].decode('ascii')

  imports = [cstr(struct.unpack_from('<I', data, h['import_off'] + 4 * k)[0]) for k in range(h['nimport'])]
  relocs = []
  for k in range(h['nreloc']):
    off, typ, zero, imp = struct.unpack_from(RELOC_FMT, data, h['reloc_off'] + 8 * k)
    relocs.append((off, typ, imp))
  return {'header': h, 'image': image, 'imports': imports, 'relocs': relocs}

def dump(data):
  x = read_ext(data)
  h = x['header']
  lines = ['%-12s %s' % (k, h[k] if k in ('magic', 'name') else '0x%x' % h[k] if k == 'crc32' else h[k])
           for k in HEADER_FIELDS]
  lines.append('crc ok' if h['crc32'] == zlib.crc32(data[HEADER_SIZE:]) & 0xFFFFFFFF else 'CRC MISMATCH')
  lines += ['import %d %s' % (i, n) for i, n in enumerate(x['imports'])]
  kinds = {ABS32: 'ABS32', ABS32_IMPORT: 'ABS32_IMPORT', THM_CALL_IMPORT: 'THM_CALL_IMPORT'}
  lines += ['reloc 0x%05x %-15s %s' % (off, kinds.get(t, t), x['imports'][i] if i != NO_IMPORT else '')
            for off, t, i in x['relocs']]
  return '\n'.join(lines)

def main(argv=None):
  ap = argparse.ArgumentParser(description='Pack an extension ELF into a DM48 .ext file.')
  ap.add_argument('elf', help='the linked ELF (or, with --dump, a .ext file)')
  ap.add_argument('-o', '--output', help='the .ext file to write')
  ap.add_argument('--name', help='the extension name (default: the output file name)')
  ap.add_argument('--version', type=lambda s: int(s, 0), default=1, help='the extension version (u32, default 1)')
  ap.add_argument('--wasm', help='a WebAssembly module to embed for the web tester')
  ap.add_argument('--dump', action='store_true', help='print the fields of a .ext file')
  a = ap.parse_args(argv)
  try:
    with open(a.elf, 'rb') as f:
      data = f.read()
    if a.dump:
      print(dump(data))
      return 0
    if not a.output:
      ap.error('-o NAME.ext is required')
    name = a.name or os.path.splitext(os.path.basename(a.output))[0]
    wasm = None
    if a.wasm:
      with open(a.wasm, 'rb') as f:
        wasm = f.read()
    out = pack(data, name, a.version, wasm)
  except PackError as e:
    print('extpack: %s: %s' % (a.elf, str(e).replace('\n', '\nextpack: %s: ' % a.elf)), file=sys.stderr)
    return 1
  with open(a.output, 'wb') as f:
    f.write(out)
  return 0

if __name__ == '__main__':
  sys.exit(main())
