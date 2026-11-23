// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * dm48ext_wasm.h - generated from dm48ext.h, which includes it when compiling for WebAssembly. Do not edit.
 * Each firmware export is imported from module "dx" by its name@major, as the web tester provides it.
 */
#define DX_WASM_IMPORT(name_at_major) __attribute__((import_module("dx"), import_name(name_at_major)))
DX_WASM_IMPORT("dx_depth@1") int dx_depth(void);
DX_WASM_IMPORT("dx_peek@1") dx_obj dx_peek(int level);
DX_WASM_IMPORT("dx_drop@1") int dx_drop(int n);
DX_WASM_IMPORT("dx_push@1") int dx_push(dx_obj o);
DX_WASM_IMPORT("dx_type@1") int dx_type(dx_obj o);
DX_WASM_IMPORT("dx_get_real@1") int dx_get_real(dx_obj o, double *out);
DX_WASM_IMPORT("dx_make_real@1") dx_obj dx_make_real(double v);
DX_WASM_IMPORT("dx_get_real_text@1") int dx_get_real_text(dx_obj o, char *buf, int cap);
DX_WASM_IMPORT("dx_make_real_text@1") dx_obj dx_make_real_text(const char *text);
DX_WASM_IMPORT("dx_get_complex@1") int dx_get_complex(dx_obj o, double *re, double *im);
DX_WASM_IMPORT("dx_make_complex@1") dx_obj dx_make_complex(double re, double im);
DX_WASM_IMPORT("dx_get_bint@1") int dx_get_bint(dx_obj o, uint64_t *out);
DX_WASM_IMPORT("dx_make_bint@1") dx_obj dx_make_bint(uint64_t v);
DX_WASM_IMPORT("dx_string_len@1") int dx_string_len(dx_obj o);
DX_WASM_IMPORT("dx_get_string@1") int dx_get_string(dx_obj o, char *buf, int cap);
DX_WASM_IMPORT("dx_make_string@1") dx_obj dx_make_string(const char *bytes, int n);
DX_WASM_IMPORT("dx_list_len@1") int dx_list_len(dx_obj o);
DX_WASM_IMPORT("dx_list_item@1") dx_obj dx_list_item(dx_obj o, int i);
DX_WASM_IMPORT("dx_make_list@1") dx_obj dx_make_list(const dx_obj *items, int n);
DX_WASM_IMPORT("dx_grob_size@1") int dx_grob_size(dx_obj o, int *w, int *h);
DX_WASM_IMPORT("dx_get_grob_bits@1") int dx_get_grob_bits(dx_obj o, uint8_t *buf, int cap);
DX_WASM_IMPORT("dx_make_grob@1") dx_obj dx_make_grob(int w, int h, const uint8_t *bits);
DX_WASM_IMPORT("dx_error@1") int dx_error(int code);
DX_WASM_IMPORT("dx_error_msg@1") int dx_error_msg(const char *msg);
DX_WASM_IMPORT("dx_poll@1") int dx_poll(void);
DX_WASM_IMPORT("dx_product@1") int dx_product(void);
DX_WASM_IMPORT("dx_fw_version@1") int dx_fw_version(char *buf, int cap);
DX_WASM_IMPORT("dx_show@1") int dx_show(int w, int h, const uint8_t *bits);
DX_WASM_IMPORT("dx_ms@1") uint32_t dx_ms(void);
DX_WASM_IMPORT("dx_key@1") int dx_key(void);
DX_WASM_IMPORT("dx_file_open@1") int dx_file_open(const char *path, int mode);
DX_WASM_IMPORT("dx_file_read@1") int dx_file_read(int f, void *buf, int n);
DX_WASM_IMPORT("dx_file_write@1") int dx_file_write(int f, const void *buf, int n);
DX_WASM_IMPORT("dx_file_seek@1") int dx_file_seek(int f, int pos);
DX_WASM_IMPORT("dx_file_tell@1") int dx_file_tell(int f);
DX_WASM_IMPORT("dx_file_size@1") int dx_file_size(int f);
DX_WASM_IMPORT("dx_file_close@1") int dx_file_close(int f);
DX_WASM_IMPORT("dx_file_delete@1") int dx_file_delete(const char *path);
DX_WASM_IMPORT("dx_file_rename@1") int dx_file_rename(const char *from, const char *to);
DX_WASM_IMPORT("dx_file_mkdir@1") int dx_file_mkdir(const char *path);
DX_WASM_IMPORT("dx_file_exists@1") int dx_file_exists(const char *path);
DX_WASM_IMPORT("dx_serial_connected@1") int dx_serial_connected(void);
DX_WASM_IMPORT("dx_serial_write@1") int dx_serial_write(const char *line, int n);
DX_WASM_IMPORT("dx_serial_read@1") int dx_serial_read(char *buf, int cap);
DX_WASM_IMPORT("dx_ir_send@1") int dx_ir_send(const uint8_t *bytes, int n);
DX_WASM_IMPORT("dx_register@1") int dx_register(const char *name, int nargs, dx_cmd_fn fn);
