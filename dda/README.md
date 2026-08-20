# dda - DOCTOR ELF 反汇编器

`dda` 将 ELF32 relocatable `.o` 反汇编为 DASM 汇编文本，使用 C99 编写。

## 构建

```sh
make
```

生成 `./dda`。

## 用法

```sh
./dda <input.o> [output.asm]
```

不提供输出文件时输出到 stdout。

## 示例

```sh
dasm/dasm -m elf program.asm program.o
dda/dda program.o program.dis.asm
```

输出的汇编格式遵循 DASM：

```asm
<指令名> [尺寸] [操作数1], [操作数2]
```
