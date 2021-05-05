#include <ir/ir.h>
#include <target/util.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// hard max is 10000 but lower is more stable
#define DESMOS_MAX_ARRAY_LEN 100
#define DESMOS_MAX_ARRAY_LEN_STR "100"
//#define DESMOS_MAX_ARRAY_LEN 100
//#define DESMOS_MAX_ARRAY_LEN_STR "100"

// this can probably be turned up to 100 but it is less stable
#define DESMOS_NUM_MEMCHUNKS 5
//#define DESMOS_NUM_MEMCHUNKS 3
// (DESMOS_NUM_MEMCHUNKS * DESMOS_MAX_ARRAY_LEN) - 1
#define DESMOS_MAX_MEM_IND "499"

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
#define DESMOS_APPEND "d"
#define DESMOS_POP "p"
#define DESMOS_INS_CHECK "k"
#define DESMOS_INC_IID "f"

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
  // 7 registers, then running, then ins_id
  for (int i = 0; i < 6; i++) {
    fputs("0,", stdout);
  }
  fputs("0,1,0\\\\right]", stdout);
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

static int brackets_to_close = -1;
static int ins_id = -1;

void desmos_emit_func_prologue(int func_id) {
  fprintf(stderr, "Emit function%d prologue\n", func_id);
  brackets_to_close = 0;
  ins_id = 0;
  desmos_start_expression();
  // assign is used to increment pc
  printf("f_{%d}\\\\left(m,o\\\\right)=", func_id);
}

void desmos_emit_func_epilogue(void) {
  fputs("o", stdout);

  for (int i = 0; i < brackets_to_close; i++) {
    fputs("\\\\right\\\\}", stdout);
  }
  desmos_end_expression();
}

#define UNUSED(x) (void)(x)

void desmos_emit_pc_change(int pc) {
  fprintf(stderr, "pc change pc=%d\n", pc);
  if (pc != 0) {
    printf(
      "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m,0,%d,%d\\\\right)=1:", pc - 1, ins_id
    );
    fputs(
      DESMOS_ASSIGN "\\\\left(" DESMOS_ASSIGN "\\\\left(o,9,-1\\\\right),7,o\\\\left[7"
      "\\\\right]+1\\\\right),", 
      stdout
    );
    ins_id = 0;
    brackets_to_close++;
  }
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
    "\\\\operatorname{mod}\\\\left(l," DESMOS_MAX_ARRAY_LEN_CONST "\\\\right)+1\\\\right]",
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

void desmos_emit_instruction_check(void) {
  desmos_start_expression();
  fputs(
    DESMOS_INS_CHECK "\\\\left(m,h,p,i\\\\right)=\\\\left\\\\{" DESMOS_REGISTERS
    "\\\\left[7\\\\right]=p:\\\\left\\\\{" DESMOS_REGISTERS "\\\\left[9\\\\right]=i:"
    "\\\\left\\\\{m=h:1,0\\\\right\\\\},0\\\\right\\\\},0\\\\right\\\\}", 
    stdout
  );
  desmos_end_expression();
}

void desmos_emit_overflow_check(void) {
  desmos_start_expression();
  fputs(DESMOS_OVERFLOW_CHECK_FUNC "\\\\left(n\\\\right)=\\\\operatorname{mod}\\\\left"
        "(n," DESMOS_MAX_MEM_IND "\\\\right)", stdout);
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

void desmos_value_string(Value* v) {
  if (v->type == REG) {
    printf("r\\\\left[%d\\\\right]", v->reg + 1);
  } else if (v->type == IMM) {
    printf("%d", v->imm);
  } else {
    error("bad value type");
  }
}

void desmos_src(Inst* inst) {
  desmos_value_string(&inst->src);
}

void desmos_reg_out(Inst *inst) {
  printf(
    "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m,0,%d,%d\\\\right)=1:"
    DESMOS_ASSIGN "\\\\left(o,%d,", 
    inst->pc, 
    ins_id, 
    inst->dst.reg + 1
  );
  brackets_to_close++;
}

void desmos_overflowed_reg_out(Inst *inst, char *join) {
  printf(
    "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m,0,%d,%d\\\\right)=1:"
    DESMOS_ASSIGN "\\\\left(o,%d," DESMOS_OVERFLOW_CHECK_FUNC 
    "\\\\left(o\\\\left[%d\\\\right]%s", 
    inst->pc, 
    ins_id, 
    inst->dst.reg + 1,
    inst->dst.reg + 1,
    join
  );
  desmos_src(inst);
  fputs("\\\\right)\\\\right),", stdout);
  brackets_to_close++;
}

void desmos_emit_mem_cond(Inst *inst) {
  fputs(
    "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m," DESMOS_GET_MEMCHUNK_NUM 
    "\\\\left(",
    stdout
  );
  if ((&inst->src)->type == REG) {
    desmos_src(inst);
    fputs("\\\\right)", stdout);
  } else if ((&inst->src)->type == IMM) {
    // If we know the value at compile time we can save a few bytes and a few CPU
    //  cycles by hardcoding the memchunk we want
    printf("%d", (&inst->src)->imm);
  } else {
    error("oops");
  }
  printf(",%d,%d\\\\right)=1:", inst->pc, ins_id);
  brackets_to_close++;
}

void desmos_emit_mem_assign(Inst *inst) {
  fputs(DESMOS_ASSIGN "\\\\left(o,\\\\operatorname{mod}\\\\left(", stdout);
  desmos_src(inst);
  fputs("," DESMOS_MAX_ARRAY_LEN_CONST "\\\\right),", stdout);
  desmos_value_string(&inst->dst);
  fputs("\\\\right),", stdout);
}

void desmos_emit_inst(Inst* inst) {
  fprintf(stderr, "Emit instruction pc=%d iid=%d src=%d\n", inst->pc, ins_id, inst->src.type == REG);

  switch (inst->op) {
  case MOV:
    desmos_reg_out(inst);
    desmos_src(inst);
    fputs("\\\\right),", stdout);
    break;

  case ADD:
    desmos_overflowed_reg_out(inst, "+");
    break;

  case SUB:
    desmos_overflowed_reg_out(inst, "-");
    break;

  case LOAD:
    desmos_reg_out(inst);
    fputs(DESMOS_MEM_ACCESSOR "\\\\left(", stdout);
    desmos_src(inst);
    fputs("\\\\right)\\\\right),", stdout);
    break;

  case STORE:
    desmos_emit_mem_cond(inst);
    desmos_emit_mem_assign(inst);
    break;

  case PUTC:
    fputs(
      "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m," DESMOS_STDOUT_MODE ",",
      stdout
    );
    printf("%d,%d\\\\right)=1:", inst->pc, ins_id);
    brackets_to_close++;
    fputs(DESMOS_APPEND "\\\\left(o,", stdout);
    desmos_src(inst);
    fputs("\\\\right),", stdout);
    break;

  case GETC:
    desmos_reg_out(inst);
    fputs(
      // check if stdin is empty, in which case we load a 0
      "\\\\left\\\\{" DESMOS_STDIN "\\\\left[1\\\\right]:" DESMOS_STDIN 
      "\\\\left[1\\\\right],0\\\\right\\\\}\\\\right),",
      stdout
    );

    // Remove the first character of stdin
    fputs(
      "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m," DESMOS_STDIN_MODE ",",
      stdout
    );
    printf("%d,%d\\\\right)=1:" DESMOS_POP "\\\\left(o\\\\right),", inst->pc, ins_id);
    brackets_to_close++;
    break;

  case EXIT:
    printf(
      "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m,0,%d,%d\\\\right)=1:", inst->pc, ins_id
    );
    brackets_to_close++;
    fputs(DESMOS_ASSIGN "\\\\left(o,8,0\\\\right),", stdout);
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
    fputs(
      "\\\\left\\\\{" DESMOS_INS_CHECK "\\\\left(m,0,",
      stdout
    );
    printf("%d,%d\\\\right)=1:" DESMOS_ASSIGN "\\\\left(o,7,", inst->pc, ins_id);
    brackets_to_close++;
    desmos_value_string(&inst->jmp);
    fputs("-1\\\\right),", stdout);
    break;

  default:
    error("oops");
  }
  ins_id++;
}

void desmos_emit_function_finder(int num_funcs) {
  desmos_start_expression();
  fputs("u_{1}\\\\left(p,m,o\\\\right)=", stdout);
  for (int i = 0; i < num_funcs; i++) {
    if (i != 0) {
      putchar(',');
    }
    printf("\\\\left\\\\{p=%d:f_{%d}\\\\left(m,o\\\\right)", i, i);
  }
  for (int i = 0; i < num_funcs; i++) {
    fputs("\\\\right\\\\}", stdout);
  }
  desmos_end_expression();
  desmos_start_expression();
  fputs(
    DESMOS_INC_IID "\\\\left(m,o\\\\right)=\\\\left\\\\{m=0:" DESMOS_ASSIGN "\\\\left("
    "o,9,o\\\\left[9\\\\right]+1\\\\right),o"
    "\\\\right\\\\}",
    stdout
  );
  desmos_end_expression();
  desmos_start_expression();
  // Having a second function helps save size because p is much shorter than accessing
  //  r[7]
  // If pc == -1, set running to 1 and pc to 0
  printf(
    "u\\\\left(m,o\\\\right)="
    "\\\\left\\\\{r[8]=1:" DESMOS_INC_IID "\\\\left(m,u_{1}\\\\left(\\\\operatorname"
    "{floor}\\\\left(\\\\frac{r\\\\left[7\\\\right]}{%d}\\\\right),m,o\\\\right)"
    "\\\\right),o\\\\right\\\\}", 
    CHUNKED_FUNC_SIZE
  );
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
  desmos_emit_instruction_check();
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
}
