#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define DESMOS_MAX_ARRAY_LEN 10000
#define DESMOS_NUM_MEMCHUNKS 100

void desmos_init_graph() {
  // getState() has a graph key with viewport info, and a randomSeed, but hese fields
  //  are actually optional for Calc.setState().
  fputs("{\"version\":8,\"expressions\":{\"list\":[", stdout);
}

void desmos_emit_id(char *s, size_t l) {
  if (l <= 1) {
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

void desmos_emit_json_escaped_string(char *p, size_t len) {
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

void desmos_start_expression(int *exp_id) {
  // getState() returns color as well but that is optional
  fputs("{\"type\":\"expression\",\"id\":", stdout);
  (*exp_id)++;
  printf("%d,\"latex\":\"", *exp_id);
}

void desmos_end_expression() {
  fputs("\"}", stdout);
}

void desmos_start_list() {
  fputs("\\\\left[", stdout);
}

void desmos_end_list() {
  fputs("\\\\right]", stdout);
}

void desmos_end_graph() {
  fputs("]}}", stdout);
}

void desmos_start_memchunk(int *exp_id, int memchunk) {
  desmos_start_expression(exp_id);
  printf("m_{em%d}=", memchunk);
  desmos_start_list();
}

void desmos_end_memchunk() {
  desmos_end_list();
  desmos_end_expression();
}

int desmos_init_mem(int *exp_id, Data *data) {
  int mp, memchunk = 0;
  desmos_start_memchunk(exp_id, 0);

  while (true) {
    if (data) {
      printf("%d", data->v);
      data = data->next;
    } else {
      putchar('0');
    }
    mp++;
    if (mp >= DESMOS_MAX_ARRAY_LEN) {
      memchunk++;
      if (memchunk >= DESMOS_NUM_MEMCHUNKS) {
        break;
      }
      mp = 0;
      desmos_end_memchunk();
      desmos_start_memchunk(exp_id, memchunk);
    } else {
      putchar(',');
    }
  }
  desmos_end_memchunk();
  return 0;
}

void target_desmos(Module *module) {
  int exp_id = 0;
  if (desmos_init_mem(&exp_id, module->data) == -1) {
    fputs("Data overflow", stderr);
    return;
  };
  return;
  desmos_init_graph();
  desmos_end_graph();
}
