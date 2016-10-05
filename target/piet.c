#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <ir/ir.h>
#include <target/util.h>

#define PIET_IMM_BASE 6

enum {
  PIET_PUSH,
  PIET_POP,
  PIET_ADD,
  PIET_SUB,
  PIET_MUL,
  PIET_DIV,
  PIET_MOD,
  PIET_NOT,
  PIET_GT,
  PIET_PTR,
  PIET_SWITCH,
  PIET_DUP,
  PIET_ROLL,
  PIET_INN,
  PIET_IN,
  PIET_OUTN,
  PIET_OUT,

  PIET_JMP,
  PIET_EXIT
} PietOp;

byte PIET_COLOR_TABLE[20][3] = {
  { 0x00, 0x00, 0x00 },
  { 0xff, 0xff, 0xff },

  { 0xff, 0xc0, 0xc0 },
  { 0xff, 0x00, 0x00 },
  { 0xc0, 0x00, 0x00 },

  { 0xff, 0xff, 0xc0 },
  { 0xff, 0xff, 0x00 },
  { 0xc0, 0xc0, 0x00 },

  { 0xc0, 0xff, 0xc0 },
  { 0x00, 0xff, 0x00 },
  { 0x00, 0xc0, 0x00 },

  { 0xc0, 0xff, 0xff },
  { 0x00, 0xff, 0xff },
  { 0x00, 0xc0, 0xc0 },

  { 0xc0, 0xc0, 0xff },
  { 0x00, 0x00, 0xff },
  { 0x00, 0x00, 0xc0 },

  { 0xff, 0xc0, 0xff },
  { 0xff, 0x00, 0xff },
  { 0xc0, 0x00, 0xc0 },
};

typedef struct PietInst {
  uint op;
  uint arg;
  struct PietInst* next;
} PietInst;

typedef struct PietBlock {
  PietInst* inst;
  struct PietBlock* next;
} PietBlock;

static void dump_piet_inst(PietInst* pi) {
  static const char* PIET_INST_NAMES[] = {
    "push",
    "pop",
    "add",
    "sub",
    "mul",
    "div",
    "mod",
    "not",
    "gt",
    "ptr",
    "switch",
    "dup",
    "roll",
    "inn",
    "in",
    "outn",
    "out",

    "jmp",
    "exit",
  };
  fprintf(stderr, "%s", PIET_INST_NAMES[pi->op]);
  if (pi->op == PIET_PUSH) {
    fprintf(stderr, " %d", pi->arg);
  }
  fprintf(stderr, "\n");
}

static void piet_emit_a(PietInst** pi, uint op, uint arg) {
  (*pi)->next = calloc(1, sizeof(PietInst));
  *pi = (*pi)->next;
  (*pi)->op = op;
  (*pi)->arg = arg;
}

static void piet_emit(PietInst** pi, uint op) {
  piet_emit_a(pi, op, 0);
}

static void piet_push_digit(PietInst** pi, uint v) {
  assert(v > 0);
  piet_emit_a(pi, PIET_PUSH, v);
}

static void piet_push(PietInst** pi, uint v) {
  if (v == 0) {
    piet_push_digit(pi, 1);
    piet_emit(pi, PIET_NOT);
    return;
  }

  uint c[16];
  uint cs = 0;
  do {
    c[cs++] = v % PIET_IMM_BASE;
    v /= PIET_IMM_BASE;
  } while (v);

  for (uint i = 0; i < cs; i++) {
    if (i != 0) {
      piet_push_digit(pi, PIET_IMM_BASE);
      piet_emit(pi, PIET_MUL);
    }
    char v = c[cs - i - 1];
    if (v) {
      piet_push_digit(pi, v);
      if (i != 0)
        piet_emit(pi, PIET_ADD);
    }
  }
}

enum {
  PIET_A = 1,
  PIET_B,
  PIET_C,
  PIET_D,
  PIET_BP,
  PIET_SP,
  PIET_MEM
};

static uint g_piet_label_id;
static uint piet_gen_label() {
  return ++g_piet_label_id;
}

static void piet_label(uint id) {
  emit_line("_track_%u:", id);
}

static void piet_pop() {
  emit_line("pop");
}

static void piet_dup() {
  emit_line("dup");
}

static void piet_br(uint id) {
  emit_line("br._track_%u", id);
}

static void piet_bz(uint id) {
  emit_line("bz._track_%u", id);
}

static void piet_roll(uint depth, uint count) {
  emit_line("%d %d roll", depth, count);
}

static void piet_rroll(uint depth, uint count) {
  emit_line("%d -%d roll", depth, count);
}

static void piet_load(uint pos) {
  piet_rroll(pos + 1, 1);
  piet_dup();
  piet_roll(pos + 2, 1);
}

static void piet_store_top(uint pos) {
  piet_rroll(pos + 2, 1);
  piet_pop();
  piet_roll(pos + 1, 1);
}

#if 0
static void piet_store(uint pos, uint val) {
  piet_push(val);
  piet_store_top(pos);
}

static void piet_init_state(Data* data) {
  //emit_line("16777223");
  emit_line("65543");  // 65536 + 7
  uint loop_id = piet_gen_label();
  uint done_id = piet_gen_label();
  piet_label(loop_id);
  emit_line("1 sub");
  emit_line("dup");
  piet_bz(done_id);
  emit_line("0");
  piet_roll(2, 1);
  piet_br(loop_id);
  piet_label(done_id);
  emit_line("");

  for (int mp = 0; data; data = data->next, mp++) {
    if (data->v) {
      piet_store(PIET_MEM + mp, data->v & 65535);
    }
  }
}
#endif

static void piet_push_value(PietInst** pi, Value* v, uint stk) {
  if (v->type == REG) {
    piet_load(PIET_A + v->reg + stk);
  } else if (v->type == IMM) {
    piet_push(pi, v->imm & 65535);
  } else {
    error("invalid value");
  }
}

static void piet_push_dst(PietInst** pi, Inst* inst, uint stk) {
  piet_push_value(pi, &inst->dst, stk);
}

static void piet_push_src(PietInst** pi, Inst* inst, uint stk) {
  piet_push_value(pi, &inst->src, stk);
}

static void piet_uint_mod() {
  //emit_line("16777216 mod");
  emit_line("65536 mod");
}

static void piet_cmp(PietInst** pi, Inst* inst, bool is_jmp) {
  Op op = normalize_cond(inst->op, is_jmp);
  if (op == JLT) {
    op = JGT;
    piet_push_src(pi, inst, 0);
    piet_push_dst(pi, inst, 1);
  } else if (op == JGE) {
    op = JLE;
    piet_push_src(pi, inst, 0);
    piet_push_dst(pi, inst, 1);
  } else {
    piet_push_dst(pi, inst, 0);
    piet_push_src(pi, inst, 1);
  }
  switch (op) {
  case JEQ:
    emit_line("sub");
    emit_line("not");
    break;
  case JNE:
    emit_line("sub");
    if (!is_jmp) {
      emit_line("not");
      emit_line("not");
    }
    break;
  case JGT:
    emit_line("gt");
    break;
  case JLE:
    emit_line("gt");
    emit_line("not");
    break;
  default:
    error("cmp");
  }
}

#if 0
static void piet_reg_jmp_table(int min_pc, int max_pc, int last_label) {
  if (min_pc + 1 == max_pc) {
    piet_pop();
    piet_br(min_pc);
    return;
  }

  int mid_pc = (min_pc + max_pc) / 2;
  piet_dup();
  piet_push(mid_pc-1);
  emit_line("gt");
  piet_bz(last_label + mid_pc);
  piet_reg_jmp_table(mid_pc, max_pc, last_label);
  piet_label(last_label + mid_pc);
  piet_reg_jmp_table(min_pc, mid_pc, last_label);
}
#endif

static void piet_emit_inst(PietInst** pi, Inst* inst) {
  switch (inst->op) {
  case MOV:
    piet_push_src(pi, inst, 0);
    piet_store_top(PIET_A + inst->dst.reg);
    break;

  case ADD:
    piet_push_dst(pi, inst, 0);
    piet_push_src(pi, inst, 1);
    emit_line("add");
    piet_uint_mod();
    piet_store_top(PIET_A + inst->dst.reg);
    break;

  case SUB:
    piet_push_dst(pi, inst, 0);
    piet_push_src(pi, inst, 1);
    emit_line("sub");
    piet_uint_mod();
    piet_store_top(PIET_A + inst->dst.reg);
    break;

  case LOAD:
    piet_push_src(pi, inst, 0);

    piet_push(pi, PIET_MEM + 1);
    emit_line("add");
    emit_line("-1 roll");
    piet_dup();

    piet_push_src(pi, inst, 0);
    piet_push(pi, PIET_MEM + 2);
    emit_line("add");
    emit_line("1 roll");

    piet_store_top(PIET_A + inst->dst.reg);
    break;

  case STORE:
    piet_push_dst(pi, inst, 0);
    piet_push_src(pi, inst, 1);
    piet_dup();

    piet_push(pi, PIET_MEM + 3);
    emit_line("add");
    emit_line("-1 roll");
    piet_pop();

    piet_push(pi, PIET_MEM + 1);
    emit_line("add");
    emit_line("1 roll");
    break;

  case PUTC:
    piet_push_src(pi, inst, 0);
    piet_emit(pi, PIET_OUT);
    break;

  case GETC: {
    piet_push(pi, 256);
    emit_line("in");
    piet_dup();
    piet_push(pi, 256);
    emit_line("sub");

    uint zero_id = piet_gen_label();
    uint done_id = piet_gen_label();

    piet_bz(zero_id);
    piet_roll(2, 1);
    piet_pop();
    piet_br(done_id);

    piet_label(zero_id);
    piet_pop();
    piet_push(pi, 0);

    piet_label(done_id);
    piet_store_top(PIET_A + inst->dst.reg);

    break;
  }

  case EXIT:
    piet_emit(pi, PIET_EXIT);
    break;

  case DUMP:
    break;

  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    piet_cmp(pi, inst, false);
    piet_store_top(PIET_A + inst->dst.reg);
    break;

  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
    piet_cmp(pi, inst, true);
    if (inst->jmp.type == REG) {
      error("jcc reg");
    } else {
      piet_bz(inst->jmp.imm);
    }
    break;

  case JMP:
    piet_push_value(pi, &inst->jmp, 0);
    piet_emit(pi, PIET_JMP);
    break;

  default:
    error("oops");
  }
}

uint piet_next_color(uint c, uint op) {
  op++;
  uint l = (c + op) % 3;
  uint h = (c / 3 + op / 3) % 6;
  return l + h * 3;
}

void target_piet(Module* module) {
  PietBlock pb_head;
  PietBlock* pb = &pb_head;
  PietInst* pi = 0;

  int prev_pc = -1;
  for (Inst* inst = module->text; inst; inst = inst->next) {
    if (prev_pc != inst->pc) {
      if (pi && pi->op != PIET_JMP) {
        piet_push(&pi, inst->pc + 1);
      }

      pb->next = calloc(1, sizeof(PietBlock));
      pb = pb->next;

      pi = calloc(1, sizeof(PietInst));
      pb->inst = pi;

      piet_emit(&pi, PIET_POP);
    }
    prev_pc = inst->pc;
    piet_emit_inst(&pi, inst);
  }

  int pc = 0;
  int longest_block = 0;
  for (pb = pb_head.next; pb; pb = pb->next) {
    pc++;
    int block_len = 0;
    for (pi = pb->inst->next; pi; pi = pi->next) {
      block_len++;
    }
    if (longest_block < block_len)
      longest_block = block_len;
  }

  pc = 0;
  for (pb = pb_head.next; pb; pb = pb->next) {
    fprintf(stderr, "\npc=%d:\n", pc++);
    for (pi = pb->inst->next; pi; pi = pi->next) {
      fprintf(stderr, " ");
      dump_piet_inst(pi);
    }
  }

  uint w = longest_block + 20;
  uint h = pc * 7 + 20;
  byte* pixels = calloc(w * h, 1);

  uint c = 0;
  uint y = 0;
  for (uint x = 0; x < w; x++) {
    pixels[y*w+x] = 1;
  }
  c = 0;
  pixels[y*w+0] = 2;
  c = piet_next_color(c, PIET_PUSH);
  pixels[y*w+1] = c + 2;
  c = piet_next_color(c, PIET_NOT);
  pixels[y*w+2] = c + 2;

  pixels[(y+1)*w+w-1] = 1;

  y += 2;
  for (uint x = 0; x < w; x++) {
    pixels[y*w+x] = 1;
  }

  c = 0;
  byte BORDER_TABLE[7];
  BORDER_TABLE[0] = 1;
  BORDER_TABLE[1] = 2;
  c = piet_next_color(c, PIET_PUSH);
  BORDER_TABLE[2] = c + 2;
  c = piet_next_color(c, PIET_SUB);
  BORDER_TABLE[3] = c + 2;
  c = piet_next_color(c, PIET_DUP);
  BORDER_TABLE[4] = c + 2;
  c = piet_next_color(c, PIET_NOT);
  BORDER_TABLE[5] = c + 2;
  c = piet_next_color(c, PIET_PTR);
  BORDER_TABLE[6] = c + 2;

  for (uint by = y + 1; by < h; by++) {
    pixels[by*w] = 1;
    pixels[by*w+w-1] = BORDER_TABLE[by%7];
  }

  y += 4;

  for (pb = pb_head.next; pb; pb = pb->next, y += 7) {
    assert(y < h);
    pixels[y*w+w-2] = 1;
    pixels[y*w+w-3] = 2;
    uint x = w - 3;
    c = 0;
    bool goto_next = true;
    for (pi = pb->inst->next; pi; pi = pi->next, x--) {
      assert(x < w);
      if (pi->op == PIET_PUSH) {
        assert(pi->arg);
        for (uint i = 0; i < pi->arg; i++) {
          pixels[(y+i)*w+x] = c + 2;
        }
      } else if (pi->op == PIET_JMP) {
        break;
      } else if (pi->op == PIET_EXIT) {
        pixels[(y+1)*w+x] = pixels[(y+0)*w+x];
        pixels[(y+1)*w+x-1] = 1;
        pixels[(y+0)*w+x-2] = 3;
        pixels[(y+1)*w+x-2] = 3;
        pixels[(y+2)*w+x-2] = 3;
        goto_next = false;
        break;
      }

      if (!goto_next)
        break;

      c = piet_next_color(c, pi->op);
      pixels[y*w+x-1] = c + 2;
    }

    if (goto_next) {
      for (x--; x > 0; x--) {
        pixels[y*w+x] = 1;
      }
    }
  }

  printf("P6\n");
  printf("\n");
  printf("%d %d\n", w, h);
  printf("255\n");

  for (uint y = 0; y < h; y++) {
    for (uint x = 0; x < w; x++) {
      byte* c = PIET_COLOR_TABLE[pixels[y*w+x]];
      putchar(c[0]);
      putchar(c[1]);
      putchar(c[2]);
    }
  }
}