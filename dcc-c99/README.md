# dcc-c99 — 用 C99 重写 dcc 的最小移植

`dcc-c99` 是现有 `dcc/` 的 **C99 重写版本**（最小可用子集）。
编译器本身使用 C99 编写，输出 DOCTOR dasm 汇编，可继续由 `dasm` 汇编、
`doctor-sim` 运行。

## 构建

```sh
cd dcc-c99
make
```

生成 `./dcc-c99`。

## 用法

```sh
./dcc-c99 input.c output.asm
dasm/dasm output.asm output.code output.data
doctor-sim/build/bin/doctor-sim -f code output.code data output.data
```

## 目录结构

```text
dcc-c99/
├── Makefile
├── README.md
├── TODO.md
├── src/
│   ├── ast.h
│   ├── ast.c
│   ├── lexer.h
│   ├── lexer.c
│   ├── parser.h
│   ├── parser.c
│   ├── codegen.h
│   ├── codegen.c
│   └── main.c
└── tests/
    └── hello.c
```

## 当前支持

- `int` / `char` / `void` / `short` / `long` / `long long` / `unsigned` / `signed` / `const` / `_Bool`
- `long` / `long long` 使用 8 字节槽位存储，支持 64 位加减乘除/比较/位运算/移位（含符号区分）
- `char` / `short` 使用 1/2 字节存储，加载时按 signed/unsigned 正确扩展；`int` 运算区分有符号/无符号
- `float` / `double` 支持存储、赋值、四则运算、比较、函数传参/返回，以及 `int` / `float` / `double` 之间的转换和强制类型转换（使用 DFE/DDE）
- `const` 只读检查：对 const 变量/参数赋值、`++`/`--`、复合赋值会报错
- `_Bool`：1 字节存储，赋值时归一化为 0/1
- 全局变量与函数
- 局部变量
- `return` / `if/else` / `while` / `for` / `break` / `continue`
- 算术、比较、逻辑、位运算、赋值、复合赋值、`++/--`、函数调用
- 注释 `//` 与 `/* */`
