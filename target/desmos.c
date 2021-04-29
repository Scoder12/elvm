#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <string.h>

void desmos_init_graph() {
  // getState() has a graph key with viewport info, and a randomSeed, but hese fields
  //  are actually optional for Calc.setState().
  fputs("{\"version\":8,\"expressions\":{\"list\":[", stdout);
}

void desmos_emit_id(char *s) {
  if (strlen(s) < 2) {
    putchar(s[0]);
  } else {
    printf("%c_{%s}", s[0], s + 1);
  }
}

// Following two functions modified from https://github.com/cesanta/frozen/blob/master/frozen.c
int desmos_get_utf8_char_len(unsigned char ch) {
  if ((ch & 0x80) == 0) return 1;
  switch (ch & 0xf0) {
    case 0xf0:
      return 4;
    case 0xe0:
      return 3;
    default:
      return 2;
  }
}

void desmos_emit_escaped_string(char *p, size_t len) {
  int cl = 0;
  const char *hex_digits = "0123456789abcdef";
  const char *specials = "btnvfr";

  for (size_t i = 0; i < len; i++) {
    unsigned char ch = p[i];
    if (ch == '"' || ch == '\\') {
      putchar('\\');
      putchar(p[i]);
    } else if (ch >= '\b' && ch <= '\r' && ch != '\v') {
      putchar('\\');
      putchar(specials[ch - '\b']);
    } else if (isprint(ch)) {
      putchar(p[i]);
    } else if ((cl = desmos_get_utf8_char_len(ch)) == 1) {
      fputs("\\u00", stdout);
      putchar(hex_digits[(ch >> 4) % 0xf]);
      putchar(hex_digits[ch % 0xf]);
    } else {
      printf("%.*s", cl, p + i);
      i += cl - 1;
    }
  }
}

void desmos_emit_expression(int exp_id, char *latex, size_t len) {
  // getState() returns color as well but that is optional
  fputs("{\"type\":\"expression\",\"id\":", stdout);
  printf("%d,\"latex\":\"", exp_id);
  desmos_emit_escaped_string(latex, len);
  fputs("\"}", stdout);
}

void desmos_end_graph() {
  fputs("]}}", stdout);
}

void init_mem(Data *data) {
  for (int mp = 0; data; data = data->next, mp++) {
    if (data->v) {
      emit_line("mem[%d] = %d;", mp, data->v);
    }
  }
}

void target_desmos(Module *module) {
  //init_mem(module->data);
  desmos_init_graph();
  char *s = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\x0b\f";
  desmos_emit_expression(0, s, strlen(s));
  desmos_end_graph();
}
