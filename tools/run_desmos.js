// This script executes a Desmos calculator state using a headless browser.
// To run this script, first install npm package 'puppeteer' under 'tools' directory (here)
// using the following command:
//     $ ls run_desmos.js # make sure you are in the same directory
//     run_desmos.js
//     $ npm install puppeteer  # this generates 'node_modules' in this directory.
// Note: this will download and run chrome and will not work in all environments without some setup.

const puppeteer = require("puppeteer");
const fs = require("fs");

// Be sure to change these to the values defined in desmos.c
const RUNNING_VARIABLE = "r";
const STDIN_VARIABLE = "s_{tdin}";
const STDOUT_VARIABLE = "s_{tdout}";

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function fatal(err) {
  process.stderr.write(msg + "\n");
  process.exit(1);
}

async function main() {
  const argv = process.argv;
  if (argv.length < 3) {
    fatal(`Usage: ${argv[0]} ${argv[1]} <filename>\n`);
  }
  const inputState = JSON.parse(await fs.promises.readFile(argv[2]));
  if (typeof inputState?.expressions?.ticker !== "object") {
    fatal("Invalid state detected");
  }
  const stdinData = await fs.readFileSync(0);
  const stdinNumbers = Array.from(Buffer.from(stdinData)).join(",");

  // must construct regex dynamically due to use of variable
  let foundStdin = false;
  const state = {
    ...inputState,
    expressions: {
      ...inputState.expressions,
      ticker: {
        ...inputState.expressions.ticker,
        // update the ticker so that it automatically plays
        playing: true,
      },
      list: inputState.expressions.list.map((exp) => {
        if (exp.latex == `${STDIN_VARIABLE}=\\left[\\right]`) {
          foundStdin = true;
          return {
            ...exp,
            latex: `${STDIN_VARIABLE}=\\left[${stdinNumbers}\\right]`,
          };
        }
        return exp;
      }),
    },
  };
  if (!foundStdin) {
    fatal(
      "Unable to find stdin variable. Check that STDIN_VARIABLE is properly set in this script."
    );
  }

  // Setup calculator
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME_EXECUTABLE_PATH, // optional executable override
    headless: true, // whether to show browser window
  });
  const page = await browser.newPage();
  // disable graphpaper for performance reasons
  await page.goto(
    "https://www.desmos.com/calculator?nographpaper&nozoomButtons",
    {
      waitUntil: "networkidle2",
    }
  );

  // Set initial state
  await page.evaluate((newState) => window.Calc.setState(newState), state);
  // Wait for graph to finish running
  await page.evaluate(
    (runningVarName, stdoutVarName) =>
      new Promise((resolve) => {
        const runningVarExp = window.Calc.HelperExpression({
          latex: runningVarName,
        });
        runningVarExp.observe("numericValue", () => {
          if (runningVarExp.numericValue == 0) {
            resolve();
          }
        });
        const stdoutExp = window.Calc.HelperExpression({
          latex: stdoutVarName,
        });
        stdoutExp.observe("listValue", () => {
          window.ELVM_STDOUT_VAL = stdoutExp.listValue;
        });
      }),
    RUNNING_VARIABLE,
    STDOUT_VARIABLE
  );
  // Get stdout
  const stdoutBytes = await page.evaluate(() => window.ELVM_STDOUT_VAL);
  // Print the stdout bytes
  process.stdout.write(String.fromCharCode(...stdoutBytes));

  await browser.close();
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
