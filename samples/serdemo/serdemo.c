// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
#include "dm48ext.h"
/* SERDEMO: lines to and from a PC over USB (SETUP's USB console ON, a program such as the kit's dm48serial.py on the
   PC), and text to an HP 82240B printer by infrared. */
static char text[4096];
static char line[256];

static int string_at(int level, int *n) {     /* the string at level into text[] */
  dx_obj o = dx_peek(level);
  int len = dx_string_len(o);
  if (len < 0) return -1;
  if (len > (int)sizeof text) return dx_error(DX_ERR_NO_MEM);
  *n = dx_get_string(o, text, (int)sizeof text);
  return *n < 0 ? -1 : 0;
}
static int sconn(void) {                       /* ( -> 1/0 ) */
  dx_obj o = dx_make_real(dx_serial_connected());
  return o ? dx_push(o) : -1;
}
static int ssend(void) {                       /* ( "text" -> ): each line of it (split at its line ends) one line */
  int n;
  if (string_at(1, &n)) return -1;
  for (int at = 0; at <= n;) {
    int end = at;
    while (end < n && text[end] != '\n') end++;
    int k = end - at;
    if (k && text[end - 1] == '\r') k--;
    while (k > DX_SERIAL_LINE) {               /* a long line goes out in pieces */
      if (dx_serial_write(text + at, DX_SERIAL_LINE)) return -1;
      at += DX_SERIAL_LINE; k -= DX_SERIAL_LINE;
    }
    if (dx_serial_write(text + at, k)) return -1;
    if (dx_poll()) return -1;
    at = end + 1;
  }
  return dx_drop(1);
}
static int push_line(int n) { dx_obj s = dx_make_string(line, n); return s ? dx_push(s) : -1; }
static int srecv(void) {                       /* ( -> "line" ): the next line received, "" when none */
  int n = dx_serial_read(line, (int)sizeof line);
  return n < 0 ? -1 : push_line(n);
}
static int swait(void) {                       /* ( ms -> "line" ): waits for a line up to ms, "" when none came */
  double d;
  if (dx_get_real(dx_peek(1), &d)) return -1;
  uint32_t t0 = dx_ms();
  for (;;) {
    int n = dx_serial_read(line, (int)sizeof line);
    if (n < 0) return -1;
    if (n > 0 || (double)(dx_ms() - t0) >= d) return dx_drop(1) ? -1 : push_line(n);
    if (dx_poll()) return -1;
  }
}
static int irsend(void) {                      /* ( "bytes" -> ): the string's bytes as they are */
  int n;
  if (string_at(1, &n)) return -1;
  return dx_ir_send((const uint8_t *)text, n) ? -1 : dx_drop(1);
}
static int irprint(void) {                     /* ( "text" -> ): one printed line (the 82240B's line end, 04) */
  int n;
  if (string_at(1, &n)) return -1;
  if (n >= (int)sizeof text) return dx_error(DX_ERR_NO_MEM);
  text[n++] = 0x04;
  return dx_ir_send((const uint8_t *)text, n) ? -1 : dx_drop(1);
}
int dx_init(void) {
  return dx_register("USBCON", 0, sconn) || dx_register("USBSEND", 1, ssend) || dx_register("USBRECV", 0, srecv)
      || dx_register("USBWAIT", 1, swait) || dx_register("IRSEND", 1, irsend) || dx_register("IRPRINT", 1, irprint);
}
