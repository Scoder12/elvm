#!/bin/bash

# Desmos generates a JSON object that represents a Desmos graph state that can be 
#  loaded using the Desmos JS API.
# This script uses run_desmos.js (nodejs) to launch a headless browser to execute the
#  state in the desmos graphing calculator.
# Launching a headless browser and running the generated code is desmos is quite slow.

<&0 node ./tools/run_desmos.js "$1"
