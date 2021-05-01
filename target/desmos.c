#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//#define DESMOS_MAX_ARRAY_LEN 10000
//#define DESMOS_NUM_MEMCHUNKS 100
#define DESMOS_MAX_ARRAY_LEN 100
#define DESMOS_NUM_MEMCHUNKS 3
// DESMOS_NUM_MEMCHUNKS + 1 for registers
#define DESMOS_COND_SIZE (DESMOS_NUM_MEMCHUNKS + 1)

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

void desmos_init_graph(void) {
  // getState() has a graph key with viewport info, and a randomSeed, but hese fields
  //  are actually optional for Calc.setState().
  fputs("{\"version\":8,\"expressions\":{\"list\":[", stdout);
}

void desmos_end_graph(void) {
  fputs("]}}", stdout);
}

static int exp_id;

void desmos_start_expression(void) {
  if (exp_id != 0) {
    putchar(',');
  }
  // getState() returns color as well but that is optional
  fputs("{\"type\":\"expression\",\"id\":", stdout);
  printf("%d,\"latex\":\"", exp_id);
  exp_id++;
}

void desmos_end_expression(void) {
  fputs("\"}", stdout);
}

void desmos_start_simulation(void) {
  if (exp_id != 0) {
    putchar(',');
  }
  fputs("{\"type\":\"simulation\",\"id\":", stdout);
  printf("%d", exp_id);
  // getState returns fps but that is optional, the default of 60 is assumed
  fputs(",\"clickableInfo\":{\"rules\":[", stdout);
  exp_id++;
}

void desmos_start_simulation_rule(int sim_start, char *assignment) {
  if (exp_id != sim_start) {
    putchar(',');
  }
  fputs("{\"id\":", stdout);
  printf("%d,\"assignment\":\"%s\",\"expression\":\"", exp_id, assignment);
  exp_id++;
}

void desmos_end_simulation_rule(void) {
  fputs("\"}", stdout);
}

void desmos_end_simulation(void) {
  fputs("]}}", stdout);
}

void desmos_start_list(void) {
  fputs("\\\\left[", stdout);
}

void desmos_end_list(void) {
  fputs("\\\\right]", stdout);
}

void desmos_init_mainloop(void) {
  desmos_start_simulation();
  int sim_start = exp_id;
  desmos_start_simulation_rule(sim_start, "r");
  fputs("u\\\\left(0,r\\\\right)", stdout);
  desmos_end_simulation_rule();
  desmos_end_simulation();
}

void desmos_init_registers(void) {
  desmos_start_expression();
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

void desmos_start_memchunk(int memchunk) {
  desmos_start_expression();
  printf("m_{em%d}=", memchunk);
}

void desmos_init_mem(Data *data) {
  int mp = 0;
  int memchunk = 0;
  desmos_start_memchunk(0);
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
      desmos_start_memchunk(memchunk);
      desmos_start_list();
    } else {
      putchar(',');
    }
  }
  desmos_end_list();
  desmos_end_expression();
  while (memchunk < DESMOS_NUM_MEMCHUNKS) {
    desmos_start_memchunk(memchunk);
    fputs("\\\\sum_{n=\\\\left[1,...10000\\\\right]}^{\\\\left[1,...10000\\\\right]}0", stdout);
    desmos_end_expression();
    memchunk++;
  }
}

typedef struct DesmosCondition_
{
  char *cond;
  char *out;
  struct DesmosCondition_ *next;
} DesmosCondition;

static DesmosCondition* desmos_current_cond[DESMOS_COND_SIZE];

void desmos_free_current_cond(void) {
  DesmosCondition *tmp, *head;
  for (int i = 0; i < DESMOS_COND_SIZE; i++) {
    fprintf(stderr, "Free register %d\n", i);
    while (head != NULL) {
      tmp = head;
      head = head->next;
      free(tmp->cond);
      free(tmp->out);
      free(tmp);
    }
  }
}

void desmos_emit_func_prologue(int func_id) {
  fprintf(stderr, "Emit function%d prologue\n", func_id);
  desmos_free_current_cond();
  desmos_start_expression();
  printf("f_{%d}\\\\left(m,o\\\\right)=", func_id);
}

void desmos_emit_func_epilogue(void) {
  fprintf(stderr, "Emit function epilogue\n");
  bool first = true;
  int opened_brackets = 0;

  for (int i = 0; i < DESMOS_COND_SIZE; i++) {
    DesmosCondition *cond = desmos_current_cond[i];
    fprintf(stderr, "  Process memchunk %d\n", i);

    if (cond) {
      if (!first) {
        putchar(',');
      }
      first = false;

      printf("\\\\left\\\\{m=%d:\\\\left\\\\{", i);
      opened_brackets++;
      int register_opened_brackets = 0;
      for (; cond; register_opened_brackets++, cond = cond->next) {
        if (register_opened_brackets != 0) {
          fputs(",\\\\left\\\\{", stdout);
        }
        printf("%s:%s", cond->cond, cond->out);
      }
      fputs(",o", stdout);
      for (int j = 0; j < register_opened_brackets; j++) {
        fputs("\\\\right\\\\}", stdout);
      }
    }
  }
  for (int j = 0; j < opened_brackets; j++) {
    fputs("\\\\right\\\\}", stdout);
  }
  desmos_end_expression();
}

#define UNUSED(x) (void)(x)

void desmos_emit_pc_change(int pc) {
  UNUSED(pc);
}

void desmos_emit_assign_function() {
  desmos_start_expression();
  fputs("", stdout);
  desmos_end_expression();
}

char* desmos_mallocd_sprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  size_t needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  char* data = malloc(needed + 1);
  va_start(ap, fmt);
  vsprintf(data, fmt, ap);
  va_end(ap);
  return data;
}

DesmosCondition* desmos_alloc_cond(int memchunk) {
  DesmosCondition *cond;
  cond = malloc(sizeof(DesmosCondition));
  cond->next = NULL;
  cond->cond = NULL;
  cond->out = NULL;

  DesmosCondition *base = desmos_current_cond[memchunk];
  if (base) {
    while(base->next) {
      base = base->next;
    }
    base->next = cond;
  } else {
    desmos_current_cond[memchunk] = cond;
  }
  return cond;
}

void desmos_emit_inst(Inst* inst) {
  switch (inst->op) {
  case MOV:
    emit_line("%s = %s;", reg_names[inst->dst.reg], src_str(inst));
    break;

  case ADD:
    emit_line("%s = (%s + %s) & " UINT_MAX_STR ";",
              reg_names[inst->dst.reg],
              reg_names[inst->dst.reg], src_str(inst));
    break;

  case SUB:
    emit_line("%s = (%s - %s) & " UINT_MAX_STR ";",
              reg_names[inst->dst.reg],
              reg_names[inst->dst.reg], src_str(inst));
    break;

  case LOAD:
    emit_line("%s = mem[%s];", reg_names[inst->dst.reg], src_str(inst));
    break;

  case STORE:
    emit_line("mem[%s] = %s;", src_str(inst), reg_names[inst->dst.reg]);
    break;

  case PUTC:
    emit_line("putchar(%s);", src_str(inst));
    break;

  case GETC:
    emit_line("%s = getchar();",
              reg_names[inst->dst.reg]);
    break;

  case EXIT:
    emit_line("running = false; break;");
    break;

  case DUMP:
    break;

  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    emit_line("%s = (%s) | 0;",
              reg_names[inst->dst.reg], cmp_str(inst, "true"));
    break;

  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
  case JMP:
    emit_line("if (%s) pc = %s - 1;",
              cmp_str(inst, "true"), value_str(&inst->jmp));
    break;

  default:
    error("oops");
  }
}

void target_desmos(Module *module) {
  desmos_init_graph();
  // Desmos functions are position independent so might as well put the mainloop at the
  //  beginning to look nice
  desmos_init_mainloop();
  desmos_init_registers();
  desmos_init_mem(module->data);
  // These functions TODO
  //int num_funcs = desmos_emit_chunked_main_loop(&exp_id);
  //desmos_emit_update_function(&exp_id, num_funcs);
  emit_chunked_main_loop(module->text,
                                         desmos_emit_func_prologue,
                                         desmos_emit_func_epilogue,
                                         desmos_emit_pc_change,
                                         desmos_emit_inst);

  desmos_end_graph();
  desmos_free_current_cond();
}
