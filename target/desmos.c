#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//#define DESMOS_MAX_ARRAY_LEN 10000
//#define DESMOS_NUM_MEMCHUNKS 100
#define DESMOS_MAX_ARRAY_LEN 100
#define DESMOS_NUM_MEMCHUNKS 3

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
  if (*exp_id != 0) {
    putchar(',');
  }
  // getState() returns color as well but that is optional
  fputs("{\"type\":\"expression\",\"id\":", stdout);
  printf("%d,\"latex\":\"", *exp_id);
  (*exp_id)++;
}

void desmos_end_expression() {
  fputs("\"}", stdout);
}

void desmos_start_simulation(int *exp_id) {
  if (*exp_id != 0) {
    putchar(',');
  }
  fputs("{\"type\":\"simulation\",\"id\":", stdout);
  printf("%d", *exp_id);
  // getState returns fps but that is optional, the default of 60 is assumed
  fputs(",\"clickableInfo\":{\"rules\":[", stdout);
  (*exp_id)++;
}

void desmos_start_simulation_rule(int *exp_id, int sim_start, char *assignment) {
  if (*exp_id != sim_start) {
    putchar(',');
  }
  fputs("{\"id\":", stdout);
  printf("%d,\"assignment\":\"%s\",\"expression\":\"", *exp_id, assignment);
  (*exp_id)++;
}

void desmos_end_simulation_rule() {
  fputs("\"}", stdout);
}

void desmos_end_simulation() {
  fputs("]}}", stdout);
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

void desmos_init_mainloop(int *exp_id) {
  desmos_start_simulation(exp_id);
  int sim_start = *exp_id;
  desmos_start_simulation_rule(exp_id, sim_start, "r");
  fputs("u\\\\left(-1\\\\right)", stdout);
  desmos_end_simulation_rule();
  desmos_end_simulation();
}

void desmos_init_registers(int *exp_id) {
  desmos_start_expression(exp_id);
  fputs("r=\\\\left[", stdout);
  // 1 running bool, 7 registers
  for (int i = 0; i < 8; i++) {
    if (i != 0) {
      putchar(',');
    }
    fputs("0", stdout);
  }
  fputs("\\\\right]", stdout);
  desmos_end_expression();
}

void desmos_start_memchunk(int *exp_id, int memchunk) {
  desmos_start_expression(exp_id);
  printf("m_{em%d}=", memchunk);
}

void desmos_init_mem(int *exp_id, Data *data) {
  int mp = 0;
  int memchunk = 0;
  desmos_start_memchunk(exp_id, 0);
  desmos_start_list();

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
      if (memchunk >= DESMOS_NUM_MEMCHUNKS || !data) {
        break;
      }
      mp = 0;
      desmos_end_list();
      desmos_end_expression();
      desmos_start_memchunk(exp_id, memchunk);
      desmos_start_list();
    } else {
      putchar(',');
    }
  }
  desmos_end_list();
  desmos_end_expression();
  while (memchunk < DESMOS_NUM_MEMCHUNKS) {
    desmos_start_memchunk(exp_id, memchunk);
    fputs("\\\\sum_{n=\\\\left[1,...10000\\\\right]}^{\\\\left[1,...10000\\\\right]}0", stdout);
    desmos_end_expression();
    memchunk++;
  }
}

void target_desmos(Module *module) {
  int exp_id = 0;
  desmos_init_graph();
  // Desmos functions are position independent so might as well put the mainloop at the
  //  beginning to look nice
  desmos_init_mainloop(&exp_id);
  desmos_init_registers(&exp_id);
  desmos_init_mem(&exp_id, module->data);
  // These functions TODO
  //int num_funcs = desmos_emit_chunked_main_loop(&exp_id);
  //desmos_emit_update_function(&exp_id, num_funcs);
  desmos_end_graph();
}
