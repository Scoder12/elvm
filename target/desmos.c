/**
 * Desmos ELVM compiler backend
 * Author: Spencer Pogorzelski (Scoder12)\
 * 
 * Runs C code in desmos by emitting JSON state data for the desmos.com online graphing
 *  calculator.
 * Attempts to generate the smallest possible state to do the job.
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
// Comment this out to make the resulting state slightly smaller.

// END OPTIONS


// CONSTANTS

// put function (puts adds a newline)
#define put(s) fputs((s), stdout)

// 4 backslashes:
// - C consumes half (2)
// - JSON consumes half (1)
// - left with 1 backslash in data
#define BSLASH "\\\\"

// define a ticker update step (must pass raw string literals)
#define ticker_update(var,val) put(var BSLASH "to " val)

// variables & functions (must be unique)
#define VAR_RUNNING "r"

// END CONSTANTS

// Helper functions

// Desmos expression IDs must be unique
//  (they use them in their UI framework like the react "key" prop)
// The UI seems to assign them sequentially, so that is what this program will do.
static int exp_id = 0;

void emit_expression(char *exp) {
  if (exp_id != 0) {
    // All but first expression must have a comma before it
    put(",");
  }

  put("{\"type\":\"expression\",\"id\":");
  printf("%d", exp_id);
  put(",\"latex\":\"");
  printf("%s", exp);
  put("\"}");

  exp_id++;
}
// End helper functions


// Graph phases
void emit_ticker_handler() {
  ticker_update("a", "a+1");
}

void emit_all_expressions() {
  emit_expression(VAR_RUNNING "=1");
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
  emit_all_expressions();
  // End expressions list
  put("]");
  // End expressions
  put("}");

  // End graph
  put("}");

  // DUMMY LINE TO AVOID "unused variable" warning
  fprintf(stderr, "%d", module->data->v);
}
