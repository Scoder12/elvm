/**
 * Desmos ELVM compiler backend
 * Author: Spencer Pogorzelski (Scoder12)
 * 
 * Runs C code in desmos by emitting JSON state data for the desmos.com online graphing
 *  calculator.
 * 
 * Usage:
 * Copy the output of this program. 
 * Open your browser's JavaScript console on a desmos calculator window.
 * Type `Calc.setState(` then paste the JSON then write `)`, then press enter.
 * Warning: Do not put the JSON in a string literal or it can mess up backslashes.
 * 
 * Change behavior by using the preprocessor constants at the top of the file.
*/
#include <ctype.h>
#include <ir/ir.h>
#include <stdlib.h>
#include <string.h>
#include <target/util.h>

// OPTIONS
// for testing purposes
#define DESMOS_MEM_SIZE 600
// END OPTIONS


// CONSTANTS

// put function (puts adds a newline)
#define put(s) fputs((s), stdout)

// 4 backslashes:
// - C consumes half (2)
// - JSON consumes half (1)
// - left with 1 backslash in data
#define BSLASH "\\\\"

#define LPAREN BSLASH "left("
#define RPAREN BSLASH "right)"
#define DESMOS_IF BSLASH "left" BSLASH "{"
#define DESMOS_THEN ":"
#define DESMOS_ELSE ","
#define DESMOS_ENDIF BSLASH "right" BSLASH "}"
#define ACTION_SETTO BSLASH "to "
#define DESMOS_LBRAC BSLASH "left["
#define DESMOS_RBRAC BSLASH "right]"
// Like scratch, desmos has mod but not binary and, so I copied this number from the
//  scratch3 target's ADD and SUB instructions. It's UINT_MAX + 1. No idea why/how
//  that works but I will use it.
#define DESMOS_UINT_MAX_STR "16777216"

// If format: { cond: truepart, falsepart }
// You can have multiple conds too:
//  { cond1: r1, cond2: r2, r3 }
// These macros handle the 1 and 2 outcome cases
#define des_if(cond,res) DESMOS_IF cond DESMOS_THEN res DESMOS_ENDIF
#define des_ifelse(cond,res,el) DESMOS_IF cond DESMOS_THEN res DESMOS_ELSE el DESMOS_ENDIF
#define des_call(func, args) func LPAREN args RPAREN
#define des_builtin(func) BSLASH "operatorname{" func "}"
#define des_array(contents) DESMOS_LBRAC contents DESMOS_RBRAC
#define inc_ip(ins) LPAREN ins "," ACTION_INC_IP RPAREN
#define des_parens(contents) LPAREN contents RPAREN
// in most cases it is most useful to set the sequence and n to the same value
#define des_sum(arr) BSLASH "sum_{n=" arr "}^{" arr "}"

// define a ticker update step (must pass raw string literals)
#define ticker_update(var,val) put(var BSLASH "to " val)

// variables, paramaters & functions
// must all be unqiue or desmos will complain
// (defined in one place to avoid overlap)
// *_FMT = has a number and will be passed to printf()
#define VAR_STDIN "s_{tdin}"
#define VAR_STDOUT "s_{tdout}"
#define VAR_RUNNING "r"
#define FUNC_CHECK "k" // preferably short since it is used a lot
#define FUNC_CHECK_PARAM0 "p"
#define FUNC_CHECK_PARAM1 "i"
#define FUNC_CHANGEPC "c_{pc}"
#define FUNC_CHANGEPC_PARAM0 "p"
#define FUNC_UPDATE "u"
#define FUNC_CALLF "c_{allf}"
#define FUNC_CALLF_PARAM0 "i"
#define FUNC_APPEND "p_{ush}"
#define FUNC_APPEND_PARAM0 "l"
#define FUNC_APPEND_PARAM1 "i"
#define FUNC_POP "p_{op}"
#define FUNC_POP_PARAM0 "l"
#define FUNC_LOAD "g"
#define FUNC_LOAD_PARAM0 "l"
#define FUNC_STORE "s"
#define FUNC_STORE_PARAM0 "l"
#define FUNC_STORE_PARAM1 "i"
#define FUNC_STORE_SUBFUNC "s_{c}"
#define FUNC_STORE_SUBFUNC_PARAM0 "h"
#define FUNC_STORE_CHUNK_FMT "s_{%d}"
#define VAR_MEMCELL_FMT "m_{%d}"
#define FUNC_ASMFUNC_FMT "f_{%d}"
#define ACTION_INC_IP "m"
// registers
#define VAR_PC "p_{c}"
// the instruction number (relative to the program counter)
// reset each time the program counter changes.
#define VAR_IP "i_{p}"
const char* desmos_reg_names[7] = {
  "a", "b", "c", "d", "b_{p}", "s_{p}", VAR_PC
};
// END CONSTANTS

// Helper functions

// Desmos expression IDs must be unique
//  (they use them in their UI framework like the react "key" prop)
// The UI seems to assign them sequentially, so that is what this program will do.
// (0 is the folder)
static int exp_id = 1;
static int folder_id = -1;

void begin_folder(char *name) {
  if (exp_id != 1) put(",");
  printf(
    "{\"type\":\"folder\",\"collapsed\":true,\"id\":%d,\"title\":\"%s\"}",
    exp_id, name
  );
  folder_id = exp_id;
  exp_id++;
}

void begin_expression(void) {
  if (exp_id != 1) put(",");
  // include "hidden": true to hide graphing variables unintentionally
  // include folderId to make expression inside of the folder
  // always include preceding comma (folder is the first item in the list)
  put("{\"type\":\"expression\",\"hidden\":true,");
  if (folder_id != -1) {
    printf("\"folderId\":\"%d\",", folder_id);
  }
  printf("\"id\":%d", exp_id);
  put(",\"latex\":\"");
}

void end_expression(void) {
  put("\"}");

  exp_id++;
}

void emit_expression(char *exp) {
  begin_expression();
  printf("%s", exp);
  end_expression();
}
// End helper functions


// Graph phases
void emit_ticker_handler() {
  put(des_call(FUNC_UPDATE, ""));
}

void emit_load_function(void) {
  begin_expression();
  put(des_call(FUNC_LOAD, FUNC_LOAD_PARAM0) "=" DESMOS_LBRAC);
  for (int mp = 0; mp < DESMOS_MEM_SIZE; mp++) {
    if (mp > 0) putchar(',');
    printf(VAR_MEMCELL_FMT, mp);
  }
  put(DESMOS_RBRAC des_array(FUNC_LOAD_PARAM0 "+1"));
  end_expression();
}

void emit_store_function(void) {
  #define DESMOS_MAX_STORE_FUNC_SIZE 100

  int start;
  int mp;
  for (
    int chunk = 0; (start = chunk * DESMOS_MAX_STORE_FUNC_SIZE) < DESMOS_MEM_SIZE; chunk++
  ) {
    begin_expression();
    printf(
      des_call(FUNC_STORE_CHUNK_FMT, FUNC_STORE_PARAM0 "," FUNC_STORE_PARAM1) "="
      DESMOS_IF,
      chunk
    );
    for (
      int i = 0; i < DESMOS_MAX_STORE_FUNC_SIZE && (mp = start + i) < DESMOS_MEM_SIZE; i++
    ) {
      if (i > 0) put(DESMOS_ELSE);
      printf(
        FUNC_STORE_PARAM0 "=%d" DESMOS_THEN VAR_MEMCELL_FMT ACTION_SETTO FUNC_APPEND_PARAM1, 
        mp, 
        mp
      );
    }
    put(DESMOS_ENDIF);
    end_expression();
  }
  
  begin_expression();
  put(
    des_call(
      FUNC_STORE_SUBFUNC, 
      FUNC_STORE_SUBFUNC_PARAM0 "," FUNC_STORE_PARAM0 "," FUNC_STORE_PARAM1
    ) "=" DESMOS_IF
  );
  for (
    int chunk = 0; chunk * DESMOS_MAX_STORE_FUNC_SIZE < DESMOS_MEM_SIZE; chunk++
  ) {
    if (chunk > 0) put(DESMOS_ELSE);
    printf(
      FUNC_STORE_SUBFUNC_PARAM0 "=%d" DESMOS_THEN 
      des_call(FUNC_STORE_CHUNK_FMT, FUNC_APPEND_PARAM0 "," FUNC_APPEND_PARAM1), 
      chunk,
      chunk
    );
  }
  put(DESMOS_ENDIF);
  end_expression();

  begin_expression();
  printf(
    des_call(FUNC_STORE, FUNC_STORE_PARAM0 "," FUNC_STORE_PARAM1) "="
    des_call(
      FUNC_STORE_SUBFUNC, 
      des_call(
        des_builtin("floor"), 
        BSLASH "frac{" FUNC_STORE_PARAM0 "}{%d}"
      ) "," FUNC_STORE_PARAM0 "," FUNC_STORE_PARAM1
    ),
    DESMOS_MAX_STORE_FUNC_SIZE
  );
  end_expression();
}

void init_state(Data* data) {
  begin_folder("IO");
  emit_expression(VAR_STDIN "=" des_array(""));
  emit_expression(VAR_STDOUT "=" des_array(""));

  // Begin registers folder
  begin_folder("Registers");
  emit_expression(VAR_RUNNING "=1");
  // Setup registers
  for (int i = 0; i < 7; i++) {
    begin_expression();
    printf("%s=0", desmos_reg_names[i]);
    end_expression();
  }
  // not technically a register
  emit_expression(VAR_IP "=0");

  begin_folder("Memory");
  // Setup memory
  int mp = 0;
  for (; data; data = data->next, mp++) {
    begin_expression();
    printf(VAR_MEMCELL_FMT "=%d", mp, data->v);
    end_expression();
  }
  for (; mp < DESMOS_MEM_SIZE; mp++) {
    begin_expression();
    printf(VAR_MEMCELL_FMT "=0", mp);
    end_expression();
  }

  begin_folder("Memory functions");
  emit_load_function();
  emit_store_function();

  begin_folder("Code");
  emit_expression(ACTION_INC_IP "=" VAR_IP ACTION_SETTO VAR_IP "+1");
}

void emit_append_function(void) {
  // append(arr, item) = 
  //  map over range(1, len(arr) + 1)
  //   (in desmos the current element is stored in the variable n)
  //  If n <= length(arr), result with arr[n]
  //  Otherwise, we are in the +1 element, so return item
  // This results in the new list arr + [item]

  // To view lists in desmos, you must make a "table" from the + menu in the top left,
  //  then put the expression that produces the list in any column after the first
  //  (the first column is special). The list will be displayed in the table.

  emit_expression(
    FUNC_APPEND des_parens(FUNC_APPEND_PARAM0 "," FUNC_APPEND_PARAM1) "="
    // des_sum(arr) is basically map(arr, n => <rest of expression>)
    des_sum(des_array("1,...," des_call(des_builtin("length"), FUNC_APPEND_PARAM0) "+1"))
    des_ifelse(
      // n is the builtin element variable in sums
      "n" BSLASH "le" des_call(des_builtin("length"), FUNC_APPEND_PARAM0),
      FUNC_APPEND_PARAM0 des_array("n"),
      FUNC_APPEND_PARAM1
    )
  );
}

void emit_pop_function(void) {
  // pop(arr) is the same as python's arr[1:]
  // how it is implemented (pseudocode)
  // if len(arr) < 2: return []
  // else: return map(range(1, len(arr)), n => arr[n])

  emit_expression(
    des_call(FUNC_POP, FUNC_POP_PARAM0) "="
    des_ifelse(
      des_call(des_builtin("length"), FUNC_POP_PARAM0) "<2",
      des_array(""),
      des_sum(
        des_array("2,...," des_call(des_builtin("length"), FUNC_POP_PARAM0))
      ) FUNC_POP_PARAM0 des_array("n")
    )
  );
}

void emit_check_function(void) {
  // Returns 1 if pc and ip matches the given paramaters.
  emit_expression(
    des_call(FUNC_CHECK, FUNC_CHECK_PARAM0 "," FUNC_CHECK_PARAM1) "="
    des_if(VAR_PC "=" FUNC_CHECK_PARAM0, des_if(VAR_IP "=" FUNC_CHECK_PARAM1, "1"))
  );
}

void emit_changepc_function(void) {
  emit_expression(
    des_call(FUNC_CHANGEPC, FUNC_CHANGEPC_PARAM0) "="
    des_ifelse(
      VAR_PC "=" FUNC_CHANGEPC_PARAM0,
      ACTION_INC_IP,
      LPAREN VAR_PC ACTION_SETTO FUNC_CHANGEPC_PARAM0 "," VAR_IP ACTION_SETTO "0" RPAREN
    )
  );
}

static int is_first_inst = 1;

void emit_func_prologue(int func_id) {
  is_first_inst = 1;
  begin_expression();
  printf(des_call(FUNC_ASMFUNC_FMT, "") "=" DESMOS_IF, func_id);
}

void emit_func_epilogue(void) {
  put(DESMOS_ENDIF);
  end_expression();
}

static int curr_pc = -1;
static int curr_ip = -1;

void next_inst(void) {
  if (is_first_inst) {
    is_first_inst = 0;
  } else {
    put(DESMOS_ELSE);
  }
  printf(des_call(FUNC_CHECK, "%d,%d") "=1" DESMOS_THEN, curr_pc, curr_ip++);
}

void emit_pc_change(int pc) {
  if (curr_pc != -1) {
    next_inst();
    printf(des_call(FUNC_CHANGEPC, "%d"), curr_pc + 1);
  }

  curr_pc = pc;
  curr_ip = 0;
}

char* desmos_value_str(Value *v) {
  if (v->type == IMM) {
    return format("%d", v->imm);
  } else if (v->type == REG) {
    return format("%s", desmos_reg_names[v->reg]);
  } else {
    error("Invalid value type %d", v->type);
  }
}

void emit_inst(Inst* inst) {
  // TODO: Group instructions as much as possible
  // For each case, you can set each variable once
  // So group until a variable that has already been set needs to be touched
  next_inst();

  switch (inst->op) {
    case MOV:
      printf(inc_ip("%s" ACTION_SETTO "%s"), desmos_reg_names[inst->dst.reg], desmos_value_str(&inst->src));
      break;

    case ADD:
      printf(
        inc_ip("%s" ACTION_SETTO des_call(des_builtin("mod"), "%s+%s," DESMOS_UINT_MAX_STR)),
        desmos_reg_names[inst->dst.reg],
        desmos_reg_names[inst->dst.reg],
        desmos_value_str(&inst->src)
      );
      break;

    case JMP:
      printf(des_call(FUNC_CHANGEPC, "%s"), desmos_value_str(&inst->jmp));
      break;

    case LOAD:
      printf(
        inc_ip("%s" ACTION_SETTO des_call(FUNC_LOAD, "%s")), 
        desmos_reg_names[inst->dst.reg],
        desmos_value_str(&inst->src)
      );
      break;

    case STORE:
      if (inst->src.type == IMM) {
        printf(
          VAR_MEMCELL_FMT ACTION_SETTO "%s", 
          inst->src.imm, 
          desmos_reg_names[inst->dst.reg]
        );
      } else {
        printf(
          inc_ip(des_call(FUNC_STORE, "%s,%s")),
          desmos_value_str(&inst->src),
          desmos_reg_names[inst->dst.reg]
        );
      }
      break;

    case EXIT:
      put(VAR_RUNNING ACTION_SETTO "0");
      break;

    case PUTC:
      printf(
        inc_ip(VAR_STDOUT ACTION_SETTO des_call(FUNC_APPEND, VAR_STDOUT ",%s")), 
        desmos_value_str(&inst->src)
      );
      break;

    case GETC:
      printf(
        inc_ip(
          "%s" ACTION_SETTO VAR_STDIN des_array("1") ","
          VAR_STDIN ACTION_SETTO des_call(FUNC_POP, VAR_STDIN)
        ), 
        desmos_reg_names[inst->dst.reg]
      );
      break;

    default:
      error("Instruction not implemented: %d", inst->op);
  }
}

void emit_update_function(int num_funcs) {
  // update function
  begin_expression();
  printf(
    des_call(FUNC_UPDATE, "") "=" 
    des_if(
      VAR_RUNNING "=1", 
      des_call(
        FUNC_CALLF, 
        des_call(des_builtin("floor"), BSLASH "frac{" VAR_PC "}{%d}")
      )
    ), 
    CHUNKED_FUNC_SIZE
  );
  end_expression();

  // callf function
  begin_expression();
  put(des_call(FUNC_CALLF, FUNC_CALLF_PARAM0) "=" DESMOS_IF);
  for (int i = 0; i < num_funcs; i++) {
    if (i != 0) put(",");
    printf(FUNC_CALLF_PARAM0 "=%d" DESMOS_THEN des_call(FUNC_ASMFUNC_FMT, ""), i, i);
  }
  put(DESMOS_ENDIF);
  end_expression();
}

// End graph phases

void target_desmos(Module *module) {
  // Setup graph.
  // getState() has a graph key with viewport info, and a randomSeed, but hese
  // fields
  //  are actually optional for Calc.setState().
  put("{\"version\":9,\"expressions\":{");

  // Setup the ticker.
  put("\"ticker\":{\"handlerLatex\":\"");
  emit_ticker_handler();
  // End ticker
  put("\",");
  put("\"open\":true,");
  // change this if you want it to start automatically
  put("\"playing\":false");
  // if "minStepLatex" is not specified it defaults to 0ms (fastest execution possible)
  put("},");

  // Begin expressions list
  put("\"list\":[");
  init_state(module->data);
  int num_funcs = emit_chunked_main_loop(
    module->text,
    emit_func_prologue,
    emit_func_epilogue,
    emit_pc_change,
    emit_inst
  );
  emit_append_function();
  emit_pop_function();
  emit_check_function();
  emit_changepc_function();
  emit_update_function(num_funcs);
  // End expressions list
  put("]");
  // End expressions
  put("}");

  // End graph
  put("}");
}
