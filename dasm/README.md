# dasm - DOCTOR 汇编器

`dasm` 是 DOCTOR 指令集的汇编器，支持生成普通二进制或 ELF32 relocatable `.o`。

## 构建

```sh
make
```

生成 `./dasm`。

## 用法

```sh
./dasm [选项] <input.asm> [output]
```

- 默认输出 `code.bin` / `data.bin`
- `-m elf`：生成 ELF32 relocatable `.o`
- `-t <file>`：输出符号表
- `EXTERN <label>`：在 ELF 模式下声明外部标号，引用处生成重定位

示例：

```sh
# 普通二进制
./dasm program.asm program.code program.data

# ELF 目标文件
./dasm -m elf program.asm program.o
```

## 语法说明

详细汇编语法见 `assembler-def.md`。

## 测试

```sh
sh ../doctor-sim/tests/run_dasm_test.sh
```

或直接运行：

```sh
make test
```
