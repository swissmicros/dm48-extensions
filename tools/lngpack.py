#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""lngpack - turn a translation of the DM48's drawn text into a language file (.lng, format 1).

  python3 lngpack.py es.json [--template ../lang/template.json] [--code es] [--name Español] [--out es.lng]
  python3 lngpack.py --dump es.lng [--template ../lang/template.json]

es.json is a copy of the kit's lang/template.json with entries replaced by their translations: a flat
{ "English": "Translation" } map, UTF-8. ../LANGUAGES.md is the translator's guide and describes the format;
HEADER_FMT below is its single definition on this side. Every rule's data (the character set, the width of each
character, what an entry must keep) comes from the template. Python 3 standard library only.
"""
import argparse
import json
import os
import re
import struct
import sys

FORMAT = 1
MAGIC = b'RLNG'
# magic, version, 3 zero bytes, catalogue, count, strings_off, size, code, name
HEADER_FMT = '<4sB3xIIII8s16s'
HEADER = struct.calcsize(HEADER_FMT)   # 48
MAX_FILE, MAX_COUNT, MAX_STR, MAX_NAME = 131072, 4096, 255, 16
BUILTIN = ('en', 'de', 'fr')           # built into the firmware: a file with one of these codes is never used
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TEMPLATE = os.path.join(os.path.dirname(HERE), 'lang', 'template.json')

# JavaScript's \s and String.prototype.trim (the firmware's own tool is TypeScript): Python's isspace() differs
# (it counts 0x1C-0x1F, which a translation may hold).
WS = '\t\n\x0b\x0c\r \u00a0\u1680' + ''.join(map(chr, range(0x2000, 0x200b))) + '\u2028\u2029\u202f\u205f\u3000\ufeff'
LEAD_WS = re.compile('[%s]' % re.escape(WS))
TRAIL_WS = re.compile('[%s]\\Z' % re.escape(WS))
NBSP_PUNCT = re.compile('^[\u202f\u00a0](?=[:;!?\u00bb%])')   # French sets a narrow no-break space before : ; ! ? »
PLACEHOLDER = re.compile(r'\{([A-Za-z0-9_]+)\}')
EXT = re.compile(r'\.[A-Z][A-Z0-9]{1,3}\b', re.ASCII)
WORD = re.compile(r'\b[A-Z][A-Z0-9]{1,}\b', re.ASCII)
SIGMA = re.compile('\u03a3[A-Z0-9]+')
UNITS = {'KB', 'MB', 'GB', 'TB'}


class LngError(Exception):
  pass


def js(s):
  """A string quoted as JSON.stringify quotes it."""
  return json.dumps(s, ensure_ascii=False)


def fnv1a(data):
  h = 0x811c9dc5
  for b in data:
    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
  return h


class Charset:
  """The calculator's 8-bit character set: the template's @charset (the text of each byte) and @aliases (other text
  for a byte, as U+202F for 0xA0). The longest text wins (x̄ is two code points)."""

  def __init__(self, tpl):
    cs = tpl['@charset']
    if len(cs) != 256:
      raise LngError('template: @charset has %d entries, not 256' % len(cs))
    self.chars = cs
    self.by_len = {}
    for s, c in [(s, c) for c, s in enumerate(cs)] + list(tpl.get('@aliases', {}).items()):
      self.by_len.setdefault(len(s), {}).setdefault(s, c)
    self.lens = sorted(self.by_len, reverse=True)

  def encode(self, text):
    out, i = [], 0
    while i < len(text):
      for n in self.lens:
        c = self.by_len[n].get(text[i:i + n])
        if c is not None:
          out.append(c)
          i += n
          break
      else:
        cp = ord(text[i])
        raise LngError('no calculator character for "%s" (U+%04X) in %s' % (text[i], cp, js(text)))
    return out

  def decode(self, data):
    return ''.join(self.chars[b] for b in data)


def upper(codes):
  """The capitals the Equation Library's headers are drawn in: a-z and à-þ (but ÷) less 0x20."""
  return [c - 0x20 if 0x61 <= c <= 0x7a or (0xe0 <= c <= 0xfe and c != 0xf7) else c for c in codes]


def device_tokens(s):
  """File extensions (.D48), then all-caps words not inside one, sorted, once each."""
  text = PLACEHOLDER.sub(lambda m: ' ' * len(m.group(0)), s)
  ext = list(EXT.finditer(text))
  words = [m.group(0) for m in WORD.finditer(text)
           if not any(e.start() <= m.start() < e.end() for e in ext) and m.group(0) not in UNITS]
  return sorted(set([e.group(0) for e in ext] + words))


def has_name(tr, name):
  """name is among tr's RPL names: an all-caps word or Σ name; a name in mixed case (Mpar) as a whole word."""
  if name != name.upper():
    return re.search(r'(?<![^\W_])%s(?![^\W_])' % re.escape(name), tr) is not None
  return name in device_tokens(tr) or name in SIGMA.findall(tr)


def structure_errors(en, tr, entry):
  """Placeholders, edge spaces and the device text (the entry's keep) of the English must stay."""
  if not tr.strip(WS):
    return ['empty']
  out = []
  pe, pt = sorted(set(PLACEHOLDER.findall(en))), sorted(set(PLACEHOLDER.findall(tr)))
  if pe != pt:
    out.append('placeholders {%s} \u2260 English {%s}' % (','.join(pt), ','.join(pe)))
  lead = lambda x: LEAD_WS.match(NBSP_PUNCT.sub('', x, count=1)) is not None
  trail = lambda x: TRAIL_WS.search(x) is not None
  if lead(en) != lead(tr):
    out.append('leading space differs')
  if trail(en) != trail(tr):
    out.append('trailing space differs')
  for tok in entry.get('keep', []):
    if tok not in tr:
      out.append('device text "%s" missing' % tok)
  return out


def control_errors(en, tr):
  out, seen = [], set()
  for ch in tr:
    if ch in seen:
      continue
    seen.add(ch)
    if ord(ch) < 0x20 and ch != '\n' and ch not in en:
      out.append('control character U+%04X' % ord(ch))
  return out


def fit_errors(cs, tpl, en, tr, entry):
  """Why tr does not fit where en is drawn (warnings). An error message: max(@cells, its English's) cells; an entry
  with px: its mini-font width (@px per byte); with cells: its bytes. A board text: none here. Raises LngError for a
  character the calculator lacks."""
  b = cs.encode(tr)
  is_entry = 'px' in entry or 'cells' in entry
  if not is_entry and entry.get('note', '').startswith('board:'):
    return []
  if not is_entry:
    limit = max(tpl['@cells'], len(cs.encode(en)))
    return ['%d cells, more than %d' % (len(b), limit)] if len(b) > limit else []
  out = []
  if 'px' in entry:
    w = sum(tpl['@px'][c] for c in (upper(b) if entry.get('upper') else b))
    if w > entry['px']:
      out.append('%d px wide, more than its %d px' % (w, entry['px']))
  if 'cells' in entry and len(b) > entry['cells']:
    out.append('%d cells, more than its %d' % (len(b), entry['cells']))
  return out


class Result:
  def __init__(self, data, warnings, untranslated):
    self.data, self.warnings, self.untranslated = data, warnings, untranslated


def build(cat, tpl, code, name, builtin=False):
  """A catalogue (English -> translation) as a .lng file: a Result (data, warnings, untranslated). Raises LngError
  listing every refusal; a translation wider than its budget, or longer than the firmware shows, is a warning."""
  cs = Charset(tpl)
  errors, warnings = [], []
  if not isinstance(code, str) or not re.fullmatch('[a-z]{2,3}', code):
    errors.append('code %s must be 2 or 3 letters a-z' % js(code))
  elif not builtin and code in BUILTIN:
    errors.append('code "%s" is a built-in language and would never be used' % code)
  nm = []
  try:
    nm = cs.encode(name)
  except LngError as e:
    errors.append('name: %s' % e)
  if not nm:
    errors.append('name is empty')
  if any(c < 0x20 for c in nm):
    errors.append('name has a control byte (a one-line label: 0x20-0xFF only)')
  if len(nm) > MAX_NAME:
    errors.append('name is %d bytes, more than %d' % (len(nm), MAX_NAME))

  entries, seen, untranslated = [], {}, 0
  for k, tx in cat.items():
    if k.startswith('@'):
      continue
    entry = tpl.get(k)
    if not isinstance(tx, str):           # a template entry left as it is
      if k in tpl:
        untranslated += 1
      else:
        errors.append('%s: not in template.json (unknown key)' % js(k))
      continue
    if not isinstance(entry, dict):
      errors.append('%s: not in template.json (unknown key)' % js(k))
      continue
    try:
      fit, charset = fit_errors(cs, tpl, k, tx, entry), []
    except LngError as e:
      fit, charset = [], [str(e)]
    errs = structure_errors(k, tx, entry)
    errs += ['RPL name "%s" missing' % n for n in entry.get('names', []) if not has_name(tx, n)]
    errs += [e for e in control_errors(k, tx) if e != 'control character U+001F']   # 0x1F is always allowed
    errors.extend('%s: %s' % (js(k), e) for e in errs + charset)
    warnings.extend('%s: %s' % (js(k), f) for f in fit)
    if charset:
      continue
    s = cs.encode(tx)
    if len(s) > MAX_STR:
      errors.append('%s: %d bytes, more than %d' % (js(k), len(s), MAX_STR))
      continue
    if any(c == 0 or (c < 0x20 and c not in (0x0a, 0x1f)) for c in s):
      continue                              # reported above
    kb = cs.encode(k)
    cap = max(2 * len(kb), len(kb) + 16)    # the firmware shows English for a longer entry
    if len(s) > cap:
      warnings.append('%s: longer than the firmware shows (max %d bytes); it will show English' % (js(k), cap))
    h = fnv1a(kb)
    if h in seen:
      errors.append('%s: same hash as %s' % (js(k), js(seen[h])))
      continue
    seen[h] = k
    entries.append((h, s))
  if len(entries) > MAX_COUNT:
    errors.append('%d entries, more than %d' % (len(entries), MAX_COUNT))
  entries.sort(key=lambda e: e[0])
  size = HEADER + 8 * len(entries) + sum(len(s) + 1 for _, s in entries)
  if size > MAX_FILE:
    errors.append('file is %d bytes, more than %d' % (size, MAX_FILE))
  if errors:
    raise LngError('language file refused:\n  ' + '\n  '.join(errors))

  keys = sorted((k for k in tpl if not k.startswith('@')), key=lambda k: k.encode('utf-16-be'))
  s_off = HEADER + 8 * len(entries)
  out = bytearray(struct.pack(HEADER_FMT, MAGIC, FORMAT, fnv1a(cs.encode('\n'.join(keys))), len(entries), s_off, size,
                              code.encode('ascii'), bytes(nm)))
  strings, off = bytearray(), 0
  for h, s in entries:
    out += struct.pack('<II', h, off)
    strings += bytes(s) + b'\0'
    off += len(s) + 1
  out += strings
  return Result(bytes(out), warnings, untranslated)


def read_lng(data):
  """A .lng file read back: code, name (bytes), version, catalogue, entries {hash: [bytes]}. Raises LngError on a
  malformed one."""
  if len(data) < HEADER or data[:4] != MAGIC:
    raise LngError('not a language file')
  _, version, catalogue, count, s_off, size, code, name = struct.unpack_from(HEADER_FMT, data)
  if size != len(data):
    raise LngError('size mismatch')
  if count > MAX_COUNT or HEADER + count * 8 > s_off or s_off > len(data):
    raise LngError('bad count or strings offset')
  entries = {}
  for i in range(count):
    h, o = struct.unpack_from('<II', data, HEADER + 8 * i)
    o += s_off
    e = data.find(b'\0', o) if o < len(data) else -1
    if e < 0 or e - o > MAX_STR:
      raise LngError('bad string offset in entry %d' % i)
    entries[h] = list(data[o:e])
  z = lambda b: b.split(b'\0', 1)[0]
  return dict(code=z(code).decode('ascii', 'replace'), name_bytes=list(z(name)), version=version, catalogue=catalogue,
              entries=entries, name=z(name).decode('latin-1'))


def load_template(path):
  with open(path, encoding='utf-8') as f:
    return json.load(f)


def dump(path, tpl):
  with open(path, 'rb') as f:
    r = read_lng(f.read())
  cs = Charset(tpl) if tpl else None
  name = cs.decode(r['name_bytes']) if cs else r['name']
  english = {}
  if tpl:
    for k in tpl:
      if not k.startswith('@'):
        english[fnv1a(cs.encode(k))] = k
    keys = sorted((k for k in tpl if not k.startswith('@')), key=lambda k: k.encode('utf-16-be'))
    same = r['catalogue'] == fnv1a(cs.encode('\n'.join(keys)))
  print('%s: version %d, code %s, name %s, %d strings, catalogue %08x%s' % (
    path, r['version'], r['code'], js(name), len(r['entries']), r['catalogue'],
    '' if not tpl else (' (this template)' if same else ' (another template)')))
  for h, s in sorted(r['entries'].items()):
    text = cs.decode(s) if cs else bytes(s).decode('latin-1')
    print('  %08x %s -> %s' % (h, js(english[h]) if h in english else '?', js(text)))


def main(argv=None):
  ap = argparse.ArgumentParser(description='Build a DM48 language file (.lng) from a translated template.')
  ap.add_argument('file', nargs='?', help='the translation, a copy of lang/template.json (UTF-8 JSON)')
  ap.add_argument('--template', default=DEFAULT_TEMPLATE, help='the kit\'s lang/template.json (default: %(default)s)')
  ap.add_argument('--code', help='the language code, 2 or 3 letters a-z (default: the file name\'s stem)')
  ap.add_argument('--name', help='the language\'s own name, at most 16 bytes (default: the file\'s "@name")')
  ap.add_argument('--out', help='the .lng to write (default: CODE.lng next to the translation)')
  ap.add_argument('--dump', metavar='LNG', help='print a .lng file instead')
  a = ap.parse_args(argv)
  try:
    if a.dump:
      dump(a.dump, load_template(a.template) if os.path.exists(a.template) else None)
      return 0
    if not a.file:
      ap.print_usage(sys.stderr)
      return 2
    tpl = load_template(a.template)
    with open(a.file, encoding='utf-8') as f:
      cat = json.load(f)
    code = a.code if a.code is not None else os.path.splitext(os.path.basename(a.file))[0]
    name = a.name if a.name is not None else cat.get('@name', '')
    r = build(cat, tpl, code, name)
    out = a.out or os.path.join(os.path.dirname(a.file), code + '.lng')
    for w in r.warnings:
      print('warning: %s' % w, file=sys.stderr)
    if os.path.dirname(out):
      os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
      f.write(r.data)
    print('lngpack: %s (%d bytes, %d strings, %d warnings)' % (out, len(r.data), len(read_lng(r.data)['entries']), len(r.warnings)))
    if r.untranslated:
      print('lngpack: %d entries left untranslated (English)' % r.untranslated)
    return 0
  except (LngError, OSError, ValueError) as e:
    print(str(e), file=sys.stderr)
    return 1


if __name__ == '__main__':
  sys.exit(main())
