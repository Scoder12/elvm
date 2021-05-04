#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//#define DESMOS_MAX_ARRAY_LEN 10000
//#define DESMOS_NUM_MEMCHUNKS 100
#define DESMOS_MAX_ARRAY_LEN 100
#define DESMOS_MAX_ARRAY_LEN_STR "100"
#define DESMOS_NUM_MEMCHUNKS 3

#define DESMOS_STDIN_MODE "1"
#define DESMOS_STDOUT_MODE "2"
#define DESMOS_MEM_MODE_OFFSET_STR "3" // registers, stdin, stdout

// Desmos function names
#define DESMOS_OVERFLOW_CHECK_FUNC "w"
#define DESMOS_REGISTERS "r"
#define DESMOS_ASSIGN "a"
#define DESMOS_ASSIGN_SUB "a_{1}"
#define DESMOS_MEM_ACCESSOR "g"
#define DESMOS_GET_MEMCHUNK_NUM "c"
#define DESMOS_GET_MEMCHUNK "t"
#define DESMOS_APPEND "h"
#define DESMOS_POP "p"

#define DESMOS_MEM_FMT "m_{em%d}"
#define DESMOS_MAX_ARRAY_LEN_CONST "b"
#define DESMOS_STDIN "s_{tdin}"
#define DESMOS_STDOUT "s_{tdout}"

#define DESMOS_MODE_REGISTERS "m=0"

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

void desmos_init_mainloop(void) {
  desmos_start_simulation();
  int sim_start = exp_id;
  desmos_start_simulation_rule(sim_start, "r");
  fputs("u\\\\left(0,r\\\\right)", stdout);
  desmos_end_simulation_rule();
  desmos_start_simulation_rule(sim_start, DESMOS_STDIN);
  fputs("u\\\\left(" DESMOS_STDIN_MODE "," DESMOS_STDIN "\\\\right)", stdout);
  desmos_end_simulation_rule();
  desmos_start_simulation_rule(sim_start, DESMOS_STDOUT);
  fputs("u\\\\left(" DESMOS_STDOUT_MODE "," DESMOS_STDOUT "\\\\right)", stdout);
  desmos_end_simulation_rule();

  for (int i = 0; i < DESMOS_NUM_MEMCHUNKS; i++) {
    char *memchunk_name = desmos_mallocd_sprintf(DESMOS_MEM_FMT, i);
    desmos_start_simulation_rule(sim_start, memchunk_name);
    printf("u\\\\left(%d,%s\\\\right)", i + 3, memchunk_name);
    free(memchunk_name);
    desmos_end_simulation_rule();
  }
  desmos_end_simulation();
}

void desmos_init_registers(void) {
  desmos_start_expression();
  fputs(DESMOS_REGISTERS "=\\\\left[", stdout);
  // 7 registers, then one running bool
  for (int i = 0; i < 6; i++) {
    fputs("0,", stdout);
  }
  // pc starts at -1 to init running to true
  fputs("-1,0\\\\right]", stdout);
  desmos_end_expression();
}

void desmos_start_memchunk(int memchunk) {
  desmos_start_expression();
  printf(DESMOS_MEM_FMT "=", memchunk);
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
    fputs("\\\\sum_{n=\\\\left[1,..."
          DESMOS_MAX_ARRAY_LEN_CONST
          "\\\\right]}^{\\\\left[1,..."
          DESMOS_MAX_ARRAY_LEN_CONST
          "\\\\right]}0", stdout);
    desmos_end_expression();
    memchunk++;
  }
}

typedef struct DesmosCondition_
{
  // unused if in register_conds
  bool free_mem_cond;
  char *mem_cond;
  int pc;
  bool free_out;
  char *out;
  struct DesmosCondition_ *next;
} DesmosCondition;

// Desmos condition logic is basically just ternary operators
// It is much shorter to do
//  is_register_mode ? (pc == 5 ? <blah>) : (pc == 6 ? <blah> : <other>)
// Than pc == 5 ? (is_register_mode ? <blah> : o) : pc == 6 ? (is_register_mode ? <blah> : o) : <other>
// Since all instructions except store write to registers, it is easier to group them
//  under one global register check, and put all the store instructions in the else
//  clause of said check. That is what these two linked lists enable. 
static DesmosCondition* desmos_register_conds = NULL;
static DesmosCondition* desmos_mem_conds = NULL;

void desmos_free_cond(DesmosCondition **cond) {
  DesmosCondition *tmp, *head;
  head = *cond;
  while (head != NULL) {
    tmp = head;
    head = head->next;
    if (tmp->mem_cond != NULL && tmp->free_mem_cond) {
      free(tmp->mem_cond);
    }
    if (tmp->out != NULL && tmp->free_out) {
      free(tmp->out);
    }
    free(tmp);
  }
  *cond = NULL; // no use-after-free
}

void desmos_free_conds(void) {
  desmos_free_cond(&desmos_register_conds);
  desmos_free_cond(&desmos_mem_conds);
}

void desmos_emit_func_prologue(int func_id) {
  fprintf(stderr, "Emit function%d prologue\n", func_id);
  desmos_free_conds();
  desmos_start_expression();
  // assign is used to increment pc
  printf("f_{%d}\\\\left(p,m,o\\\\right)=" DESMOS_ASSIGN "\\\\left(", func_id);
}

void desmos_emit_cond(DesmosCondition* cond, bool use_condition) {
  int brackets_to_close = 0;
  for (DesmosCondition* head = cond; head != NULL; head = head->next) {
    // check pc == cond->pc
    printf("\\\\left\\\\{p=%d:", cond->pc);
    brackets_to_close++;

    if (use_condition) {
      // check custom conditon
      fputs("\\\\left\\\\{", stdout);
      printf("%s:", cond->mem_cond);
    }

    // if true, return cond->out
    printf("%s,", cond->out);

    if (use_condition) {
      // if pc passes but custom cond doesn't, don't modify
      fputs("o\\\\right\\\\},", stdout);
    }
  }
  // if no pc checks pass, don't modify
  fputs("o", stdout);
  // close all the pc checks
  for (; brackets_to_close > 0; brackets_to_close--) {
    fputs("\\\\right\\\\}", stdout);
  }
}

void desmos_emit_func_epilogue(void) {
  if (desmos_register_conds != NULL) {
    fputs("\\\\left\\\\{m=0:", stdout);
    desmos_emit_cond(desmos_register_conds, false);
    putchar(',');
  }

  if (desmos_mem_conds == NULL) {
    desmos_emit_cond(desmos_mem_conds, true);
  } else if (desmos_register_conds != NULL) {
    // ensure there is a right side of the expression
    putchar('o');
  }

  if (desmos_register_conds != NULL) {
    fputs("\\\\right\\\\}", stdout);
  }

  // close pc increment assign call from prologue
  fputs(",7," DESMOS_REGISTERS "\\\\left[7\\\\right]+1\\\\right)", stdout);
  desmos_end_expression();
}

#define UNUSED(x) (void)(x)

void desmos_emit_pc_change(int pc) {
  UNUSED(pc);
}

void desmos_emit_assign_function(void) {
  desmos_start_expression();
  fputs(
    DESMOS_ASSIGN_SUB 
    "\\\\left(l_{asn},i_{asn},v_{asn},t_{asn}\\\\right)=\\\\sum_{n=t_{asn}}^{"
    "t_{asn}}\\\\left\\\\{i_{asn}=n:v_{asn},l_{asn}\\\\left[n\\\\right]\\\\right\\"
    "\\}", 
    stdout
  );
  desmos_end_expression();
  desmos_start_expression();
  fputs(DESMOS_ASSIGN "\\\\left(l_{asn},i_{asn},v_{asn}\\\\right)=" DESMOS_ASSIGN_SUB 
        "\\\\left(l_{asn},i_{asn},v_{asn},\\\\left[1,...,\\\\operatorname{length}"
        "\\\\left(l_{asn}\\\\right)\\\\right]\\\\right)", stdout);
  desmos_end_expression();
}

void desmos_emit_append_function(void) {
  desmos_start_expression();
  fputs(
    DESMOS_APPEND
    "\\\\left(l_{a},i\\\\right)=\\\\sum_{n=\\\\left[1,...,\\\\operatorname{"
    "length}\\\\left(l_{a}\\\\right)+1\\\\right]}^{\\\\left[1,...,\\\\operatorname{"
    "length}\\\\left(l_{a}\\\\right)+1\\\\right]}\\\\left\\\\{n\\\\le\\\\operatorname{"
    "length}\\\\left(l_{a}\\\\right):l_{a}\\\\left[n\\\\right],i\\\\right\\\\}",
    stdout
  );
  desmos_end_expression();
}

void desmos_emit_pop_function(void) {
  desmos_start_expression();
  fputs(
    DESMOS_POP "\\\\left(l_{s}\\\\right)=\\\\left\\\\{\\\\operatorname{length}"
    "\\\\left(l_{s}\\\\right)>0:\\\\sum_{n=\\\\left[1,...,\\\\operatorname{length}"
    "\\\\left(l_{s}\\\\right)-1\\\\right]}^{\\\\left[1,...,\\\\operatorname{length}"
    "\\\\left(l_{s}\\\\right)-1\\\\right]}l_{s}\\\\left[n+1\\\\right],l_{s}\\\\right"
    "\\\\}",
    stdout
  );
  desmos_end_expression();
}

void desmos_emit_mem_accessor(void) {
  desmos_start_expression();
  fputs(
    DESMOS_MEM_ACCESSOR "\\\\left(l\\\\right)=" DESMOS_GET_MEMCHUNK "\\\\left("
    // minus because GET_MEMCHUNK_NUM returns +1
    DESMOS_GET_MEMCHUNK_NUM "\\\\left(l\\\\right)-" DESMOS_MEM_MODE_OFFSET_STR 
    "\\\\right)\\\\left["
    "\\\\operatorname{mod}\\\\left(l," DESMOS_MAX_ARRAY_LEN_CONST "\\\\right)\\\\right]",
    stdout);
  desmos_end_expression();
  desmos_start_expression();
  fputs(
    DESMOS_GET_MEMCHUNK_NUM "\\\\left(l\\\\right)=\\\\operatorname{floor}"
    "\\\\left(\\\\frac{l}{"
    DESMOS_MAX_ARRAY_LEN_CONST
    // + to save space when comparing the result of this function against mode
    //  since m=0 is registers and m=1 is memchunk 0, and GET_MEMCHUNK_NUM(n)+1
    //  would be needed to account for registers. When actually accessing this is
    //  offset. Therefore GET_MEMCHUNK_NUM(0) is actually 1 but becomes 0 before
    //  being passed to GET_MEMCHUNK.
    "}\\\\right)+" DESMOS_MEM_MODE_OFFSET_STR, stdout);
  desmos_end_expression();
  desmos_start_expression();
  fputs(DESMOS_GET_MEMCHUNK "\\\\left(l\\\\right)=", stdout);
  for (int i = 0; i < DESMOS_NUM_MEMCHUNKS; i++) {
    if (i != 0) {
      putchar(',');
    }
    printf("\\\\left\\\\{l=%d:" DESMOS_MEM_FMT, i, i);
  }
  for (int i = 0; i < DESMOS_NUM_MEMCHUNKS; i++) {
    fputs("\\\\right\\\\}", stdout);
  }
  desmos_end_expression();
}

void desmos_emit_overflow_check(void) {
  desmos_start_expression();
  fputs(DESMOS_OVERFLOW_CHECK_FUNC "\\\\left(n\\\\right)=\\\\operatorname{mod}\\\\left"
        "(n," UINT_MAX_STR "\\\\right)", stdout);
  desmos_end_expression();
}

void desmos_emit_array_len_const(void) {
  desmos_start_expression();
  fputs(DESMOS_MAX_ARRAY_LEN_CONST "=" DESMOS_MAX_ARRAY_LEN_STR, stdout);
  desmos_end_expression();
}

void desmos_init_io(void) {
  // stdin
  desmos_start_expression();
  fputs(DESMOS_STDIN "=\\\\left[\\\\right]", stdout);
  desmos_end_expression();

  // stdout
  desmos_start_expression();
  fputs(DESMOS_STDOUT "=\\\\left[\\\\right]", stdout);
  desmos_end_expression();
}

DesmosCondition* desmos_append_cond(DesmosCondition **base) {
  DesmosCondition *cond = malloc(sizeof(DesmosCondition));
  cond->mem_cond = NULL;
  cond->pc = -1;
  cond->out = NULL;
  cond->next = NULL;

  if (*base != NULL) {
    DesmosCondition* tail = *base;
    while (tail->next != NULL) {
      tail = tail->next;
    }
    tail->next = cond;
  } else {
    *base = cond;
  }
  return cond;
}

DesmosCondition* desmos_append_reg_cond(Inst* inst, char *out, bool free_out) {
  DesmosCondition *cond = desmos_append_cond(&desmos_register_conds);
  cond->pc = inst->pc;
  cond->out = out;
  cond->free_mem_cond = false;
  cond->free_out = free_out;
  return cond;
}

DesmosCondition* desmos_append_mem_cond(Inst *inst, char *mem_cond, bool free_mem_cond, char *out, bool free_out) {
  DesmosCondition* cond = desmos_append_cond(&desmos_mem_conds);
  cond->pc = inst->pc;
  cond->out = out;
  cond->free_mem_cond = free_mem_cond;
  cond->free_out = free_out;
  cond->mem_cond = mem_cond;
  return cond;
}

char* desmos_value_string(Value* v) {
  if (v->type == REG) {
    return desmos_mallocd_sprintf("r\\\\left[%d\\\\right]", v->reg + 1);
  } else if (v->type == IMM) {
    return desmos_mallocd_sprintf("%d", v->imm);
  } else {
    error("bad value type");
  }
}

char* desmos_src(Inst* inst) {
  return desmos_value_string(&inst->src);
}

char* desmos_assign(char *args) {
  char* r = desmos_mallocd_sprintf(DESMOS_ASSIGN "\\\\left(%s\\\\right)", args);
  free(args);
  return r;
}

// convienience function for reg = tranform(reg)
void desmos_reg_out(Inst *inst, char *new_val) {
  desmos_append_reg_cond(inst, desmos_mallocd_sprintf(
    DESMOS_ASSIGN "\\\\left(" DESMOS_REGISTERS ",%d,%s\\\\right)",
    inst->dst.reg, new_val
  ), true);
  free(new_val);
}

void desmos_overflowed_reg_out(Inst *inst, char *join) {
  char *val = desmos_src(inst);
  desmos_append_reg_cond(inst, desmos_mallocd_sprintf(
    DESMOS_ASSIGN "\\\\left(" DESMOS_REGISTERS ",%d," DESMOS_OVERFLOW_CHECK_FUNC 
    "\\\\left(" DESMOS_REGISTERS "\\\\left[%d\\\\right]%s%s\\\\right)\\\\right)",
    inst->dst.reg, inst->dst.reg, join, val
  ), true);
  free(val);
}

char* desmos_gen_mem_cond(Inst *inst) {
  Value *v = &inst->src;
  if (v->type == REG) {
    char *val = desmos_value_string(v);
    char *ret = desmos_mallocd_sprintf(
      "m=" DESMOS_GET_MEMCHUNK "\\\\left(%s\\\\right)", 
      val
    );
    free(val);
    return ret;
  } else if (v->type == IMM) {
    // If we know the value at compile time we can save a few bytes and a few CPU
    //  cycles by hardcoding the memchunk we want
    return desmos_mallocd_sprintf(
      "m=%d", 
      // imm should be >0 so this should floor
      v->imm / DESMOS_MAX_ARRAY_LEN
    );
  } else {
    error("oops");
  }
}

char* desmos_gen_mem_assign(Inst *inst) {
  Value *addr = &inst->src;
  Value *val = &inst->dst;
  char *addr_val = desmos_value_string(addr);
  char *val_val = desmos_value_string(val);
  // TODO: Assign max array len to a variable so it can be reused easily
  char *ret = desmos_mallocd_sprintf(
    DESMOS_ASSIGN "\\\\left(o,\\\\operatorname{mod}\\\\left(%s," 
    DESMOS_MAX_ARRAY_LEN_CONST "\\\\right),%s\\\\right)",
    addr_val,
    val_val
  );
  free(addr_val);
  free(val_val);
  return ret;
}

void desmos_emit_inst(Inst* inst) {
  switch (inst->op) {
  case MOV:
    desmos_reg_out(inst, desmos_src(inst));
    break;

  case ADD:
    desmos_overflowed_reg_out(inst, "+");
    break;

  case SUB:
    desmos_overflowed_reg_out(inst, "-");
    break;

  case LOAD:
    {
      char *val = desmos_src(inst);
      char *new = desmos_mallocd_sprintf(
        DESMOS_MEM_ACCESSOR "\\\\left(%s\\\\right)", val
      );
      free(val);
      desmos_reg_out(inst, new);
    }
    break;

  case STORE:
    desmos_append_mem_cond(
      inst, desmos_gen_mem_cond(inst), true, desmos_gen_mem_assign(inst), true
    );
    break;

  case PUTC:
    desmos_append_mem_cond(
      inst, 
      "m=" DESMOS_STDOUT_MODE, 
      false,
      desmos_mallocd_sprintf(DESMOS_APPEND "\\\\left(o,%s\\\\right)", desmos_src(inst)),
      true
    );
    break;

  case GETC:
    emit_line("%s = getchar();",
              reg_names[inst->dst.reg]);
    desmos_append_reg_cond(
      inst, 
      // ensure we don't load an "undefined"
      "\\\\left\\\\{" DESMOS_STDIN "\\\\left[1\\\\right]:" DESMOS_STDIN 
      "\\\\left[1\\\\right],0\\\\right\\\\}",
      false
    );
    // Remove the first char of stdin
    desmos_append_mem_cond(
      inst, "m=" DESMOS_STDIN_MODE, false,
      desmos_mallocd_sprintf(DESMOS_POP "\\\\left(o\\\\right)"), true
    );
    break;

  case EXIT:
    desmos_append_reg_cond(
      inst, DESMOS_ASSIGN "\\\\left(" DESMOS_REGISTERS ",8,0\\\\right)", false
    );
    break;

  case DUMP:
    break;

  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    error("Comparison not implemented");
    emit_line("%s = (%s) | 0;",
              reg_names[inst->dst.reg], cmp_str(inst, "true"));
    break;

  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
    error("Conditional jump not implemented");
    break;

  case JMP:
    desmos_append_reg_cond(
      inst, desmos_mallocd_sprintf(DESMOS_ASSIGN "\\\\left(r,7,%s-1\\\\right)", value_str(&inst->jmp)), true
    );
    break;

  default:
    error("oops");
  }
}

void desmos_emit_function_finder(int num_funcs) {
  desmos_start_expression();
  fputs("u_{1}\\\\left(p,m,o\\\\right)=", stdout);
  for (int i = 0; i < num_funcs; i++) {
    if (i != 0) {
      putchar(',');
    }
    printf("\\\\left\\\\{p=%d:f_{%d}\\\\left(p,m,o\\\\right)", i, i);
  }
  for (int i = 0; i < num_funcs; i++) {
    fputs("\\\\right\\\\}", stdout);
  }
  desmos_end_expression();
  desmos_start_expression();
  // Having a second function helps save size because p is much shorter than accessing
  //  r[7]
  // If pc == -1, set running to 1 and pc to 0
  fputs("u\\\\left(m,o\\\\right)=\\\\left\\\\{r\\\\left[7\\\\right]=-1:\\\\left\\\\{"
        "m=0:a\\\\left(a\\\\left(r,8,1\\\\right),7,0\\\\right),o\\\\right\\\\},"
        "\\\\left\\\\{r[8]=1:u_{1}\\\\left(r\\\\left[1\\\\right],m,o\\\\right),o"
        "\\\\right\\\\}\\\\right\\\\}", stdout);
  desmos_end_expression();
}

void target_desmos(Module *module) {
  desmos_init_graph();
  desmos_init_io();
  // Desmos functions are position independent so might as well put the mainloop at the
  //  beginning to look nice
  desmos_init_mainloop();
  desmos_init_registers();
  desmos_emit_assign_function();
  desmos_emit_append_function();
  desmos_emit_mem_accessor();
  desmos_emit_overflow_check();
  desmos_emit_array_len_const();
  desmos_init_mem(module->data);
  // These functions TODO
  //int num_funcs = desmos_emit_chunked_main_loop(&exp_id);
  //desmos_emit_update_function(&exp_id, num_funcs);
  int num_funcs = emit_chunked_main_loop(module->text,
                                         desmos_emit_func_prologue,
                                         desmos_emit_func_epilogue,
                                         desmos_emit_pc_change,
                                         desmos_emit_inst);
  desmos_emit_function_finder(num_funcs);
  desmos_end_graph();
  desmos_free_conds();
}
