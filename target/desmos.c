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
#define DESMOS_MEM_SIZE 32
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
#define BUILTIN_FLOOR BSLASH "operatorname{floor}"

// If format: { cond: truepart, falsepart }
// You can have multiple conds too:
//  { cond1: r1, cond2: r2, r3 }
// These macros handle the 1 and 2 outcome cases
#define des_if(cond,res) DESMOS_IF cond DESMOS_THEN res DESMOS_ENDIF
#define des_ifelse(cond,res,el) DESMOS_IF cond DESMOS_THEN res DESMOS_ELSE el DESMOS_ENDIF

#define des_call(func, args) func LPAREN args RPAREN

// define a ticker update step (must pass raw string literals)
#define ticker_update(var,val) put(var BSLASH "to " val)

// variables, paramaters & functions
// must all be unqiue or desmos will complain
// (defined in one place to avoid overlap)
// *_FMT = has a number and will be passed to printf()
#define VAR_RUNNING "r"
#define FUNC_CHECK "k" // preferably short since it is used a lot
#define FUNC_CHECK_PARAM0 "p"
#define FUNC_CHECK_PARAM1 "i"
#define FUNC_CHANGEPC "c_{pc}"
#define FUNC_CHANGEPC_PARAM0 "p"
#define FUNC_UPDATE "u"
#define FUNC_CALLF "c_{allf}"
#define FUNC_CALLF_PARAM0 "i"
#define VAR_MEMCELL_FMT "m_{%d}"
#define FUNC_ASMFUNC_FMT "f_{%d}"
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
  put("{\"type\":\"expression\",\"hidden\":true,\"folderId\":\"");
  printf("%d\",\"id\":%d", folder_id, exp_id);
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

void init_state(Data* data) {
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
  begin_folder("Code");
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
    VAR_PC ACTION_SETTO FUNC_CHANGEPC_PARAM0 "," VAR_IP ACTION_SETTO "0"
  );
}

static int is_first_inst = 1;

void emit_func_prologue(int func_id) {
  fprintf(stderr, "begin func %d\n", func_id);
  is_first_inst = 1;
  begin_expression();
  printf(des_call(FUNC_ASMFUNC_FMT, "") "=" DESMOS_IF, func_id);
}

void emit_func_epilogue(void) {
  fprintf(stderr, "end func\n");
  put(DESMOS_ENDIF);
  end_expression();
}

static int curr_pc = -1;
static int curr_ip = -1;

void emit_pc_change(int pc) {
  fprintf(stderr, "  pc change to %d\n", pc);
  curr_pc = pc;
  curr_ip = 0;
}

char* desmos_value_str(Value *v) {
  if (v->type == IMM) {
    return format("%d", v->imm - 1);
  } else if (v->type == REG) {
    return format("%s-1", desmos_reg_names[v->reg]);
  } else {
    error("Invalid value type %d", v->type);
  }
}

void emit_inst(Inst* inst) {
  // TODO: Group instructions as much as possible
  // For each case, you can set each variable once
  // So group until a variable that has already been set needs to be touched

  if (is_first_inst) {
    is_first_inst = 0;
  } else {
    put(DESMOS_ELSE);
  }
  printf(des_call(FUNC_CHECK, "%d,%d") "=1" DESMOS_THEN, curr_pc, curr_ip++);

  switch (inst->op) {
    case JMP:
      printf(des_call(FUNC_CHANGEPC, "%s"), desmos_value_str(&inst->jmp));
      break;

    case EXIT:
      put(VAR_RUNNING ACTION_SETTO "0");
      break;

    default:
      error("Instruction not implemented: %d", inst->op);
  }
}

void emit_update_function(int num_funcs) {
  // update function
  fprintf(stderr, "Generated %d funcs", num_funcs);
  begin_expression();
  printf(
    des_call(FUNC_UPDATE, "") "=" 
    des_if(
      VAR_RUNNING "=1", 
      des_call(
        FUNC_CALLF, 
        des_call(BUILTIN_FLOOR, BSLASH "frac{" VAR_PC "}{%d}")
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
  put("\"playing\":true");
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
  emit_check_function();
  emit_changepc_function();
  emit_update_function(num_funcs);
  // End expressions list
  put("]");
  // End expressions
  put("}");

  // End graph
  put("}");

  // DUMMY LINE TO AVOID "unused variable" warning
  fprintf(stderr, "%d\n", module->data->v);
}
