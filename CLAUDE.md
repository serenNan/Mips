# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **SysY-to-MIPS compiler** (C-like subset language to MIPS assembly). The compiler follows a standard pipeline:

1. **Lexical Analysis** → Token stream
2. **Syntax/Semantic Analysis** → AST with error checking
3. **LLVM IR Generation** → Intermediate representation
4. **MIPS Code Generation** → Target assembly

## Architecture

### Main Components

- **compiler.cpp**: The front-end containing:
  - Lexer (词法分析): Tokenizes source code
  - Parser (语法分析): Recursive descent parser with semantic checks
  - IRGenerator: Produces LLVM-style IR
  - Error handling with codes (a-m) for different semantic errors

- **MipsGenerator.h/cpp**: The back-end that:
  - Parses LLVM IR text format
  - Manages register allocation using LRU strategy ($t0-$t9)
  - Handles stack frame management with $fp
  - Outputs MIPS assembly

### Key Data Structures

- `Symbol`: Stores variable/function metadata including LLVM IR names
- `IRValue`: Represents LLVM IR values with name and type
- `RegInfo`: Tracks register state (busy, dirty, last_use for LRU)

### Calling Convention

- Uses $a0-$a3 for first 4 arguments
- Return value in $v0
- Stack frame: 2048 bytes, managed via $fp and $sp
- Syscall numbers: getint(5), putint(1), putstr(4), putch(11)

## Build Commands

```fish
# Compile the project
g++ -std=c++17 -o compiler compiler.cpp MipsGenerator.cpp

# Run with test case
./compiler 测试程序库/A/testcase1/testfile.txt
```

## Test Structure

Test cases are in `测试程序库/` organized by difficulty (A, B, C):
- `testfile.txt`: SysY source code
- `in.txt`: Standard input for the program
- `ans.txt`: Expected output

## Language Features (SysY subset)

- Types: `int`, `void`, `const int`
- Arrays with initializers
- Functions with parameters
- Control flow: `if-else`, `for`, `break`, `continue`
- Operators: arithmetic, comparison, logical (`&&`, `||`, `!`)
- I/O: `getint()`, `printf()` with format strings
- Static local variables
