// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
#include "dm48ext.h"
/* FILEDEMO: files from RPL. Paths are strings ("/DM48/DEMO/A.TXT"); errors are the calculator's (No such file ...). */
static char p1[128], p2[128];
static char data[8192];
#define CHUNK 1024   /* read and write in pieces, dx_poll between them: ON breaks a long transfer */

static int path_at(int level, char *out) {   /* the string at level as a C string */
  dx_obj o = dx_peek(level);
  int len = dx_string_len(o);
  if (len < 0) return -1;
  if (len > 127) return dx_error(DX_ERR_BAD_NAME);
  if (dx_get_string(o, out, 127) < 0) return -1;
  out[len] = 0;
  return 0;
}
static int put(int mode) {                   /* ( "text" "path" -> ) */
  if (path_at(1, p1)) return -1;
  int n = dx_get_string(dx_peek(2), data, (int)sizeof data);
  if (n < 0) return -1;
  if (dx_string_len(dx_peek(2)) > n) return dx_error(DX_ERR_NO_MEM);
  int f = dx_file_open(p1, mode);
  if (f < 0) return -1;
  for (int at = 0; at < n; at += CHUNK) {
    if (dx_file_write(f, data + at, n - at < CHUNK ? n - at : CHUNK) < 0) return -1;
    if (dx_poll()) return -1;
  }
  if (dx_file_close(f)) return -1;
  return dx_drop(2);
}
static int fwrite_cmd(void) { return put(DX_FILE_WRITE); }
static int fappend_cmd(void) { return put(DX_FILE_APPEND); }
static int get_from(int pos, int levels) {   /* the file at p1 from pos, as a string replacing `levels` levels */
  int f = dx_file_open(p1, DX_FILE_READ);
  if (f < 0) return -1;
  int size = dx_file_size(f);
  if (size < 0) return -1;
  if (pos < 0 || pos > size) return dx_error(DX_ERR_BAD_VALUE);
  if (size - pos > (int)sizeof data) return dx_error(DX_ERR_NO_MEM);
  if (dx_file_seek(f, pos)) return -1;
  int n = 0;
  for (;;) {
    int k = dx_file_read(f, data + n, (int)sizeof data - n < CHUNK ? (int)sizeof data - n : CHUNK);
    if (k < 0) return -1;
    if (k == 0) break;
    n += k;
    if (dx_poll()) return -1;
  }
  dx_file_close(f);
  dx_obj s = dx_make_string(data, n);
  if (!s || dx_drop(levels)) return -1;
  return dx_push(s);
}
static int fread_cmd(void) { return path_at(1, p1) ? -1 : get_from(0, 1); }          /* ( "path" -> "text" ) */
static int ftail_cmd(void) {                                                            /* ( "path" n -> "text" ) */
  double d;
  if (path_at(2, p1) || dx_get_real(dx_peek(1), &d)) return -1;
  return get_from((int)d, 2);
}
static int fdel_cmd(void) { return path_at(1, p1) || dx_file_delete(p1) ? -1 : dx_drop(1); }      /* ( "path" -> ) */
static int fmkdir_cmd(void) { return path_at(1, p1) || dx_file_mkdir(p1) ? -1 : dx_drop(1); }    /* ( "path" -> ) */
static int fren_cmd(void) {                                                             /* ( "from" "to" -> ) */
  return path_at(2, p1) || path_at(1, p2) || dx_file_rename(p1, p2) ? -1 : dx_drop(2);
}
static int fexists_cmd(void) {                                                          /* ( "path" -> 0/1 ) */
  if (path_at(1, p1)) return -1;
  int r = dx_file_exists(p1);
  dx_obj o = r < 0 ? 0 : dx_make_real(r);
  return !o || dx_drop(1) ? -1 : dx_push(o);
}
int dx_init(void) {
  return dx_register("FWRITE", 2, fwrite_cmd) || dx_register("FAPPEND", 2, fappend_cmd) || dx_register("FREAD", 1, fread_cmd)
      || dx_register("FTAIL", 2, ftail_cmd) || dx_register("FDEL", 1, fdel_cmd) || dx_register("FMKDIR", 1, fmkdir_cmd)
      || dx_register("FREN", 2, fren_cmd) || dx_register("FEXISTS", 1, fexists_cmd);
}
