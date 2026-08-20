# dlinker - DOCTOR 最小链接器（C99 实现）

`dlinker` 是一个最小的 DOCTOR 链接器，用于把 `dasm -m elf` 生成的多个 ELF32 relocatable `.o` 文件链接成 DOCTOR 可加载的二进制文件。当前实现使用标准 C99 编写，不依赖 C++ 或 GNU 扩展。

## 构建

```sh
make
```

生成 `./dlinker`。

依赖：C99 编译器（gcc/clang）。

## 用法

```sh
./dlinker [选项] file1.o file2.o ...
```

常用选项：

- `-o <base>`：输出文件基名
  - 生成 `<base>_code.bin`
  - 生成 `<base>_data.bin`
- `-t <file>`：可选，输出链接后的符号表
- `-Ttext <addr>`：设置代码段运行时基址（默认 `0`）
- `-Tdata <addr>`：设置数据段运行时基址（默认 `0`）
- `-v, --verbose`：显示详细链接信息
- `--no-fallback`：禁用旧格式扫描回退，要求输入必须带 ELF 重定位表
- `-h, --help`：显示帮助

默认输出基名为第一个输入文件去掉 `.o` 后的名字。

## 链接规则

- 所有 `.o` 的 `.text` 段按输入顺序拼接，写入 `*_code.bin`
- 所有 `.o` 的 `.data` / `.rodata` 段按输入顺序拼接，写入 `*_data.bin`
- `.bss`（NOBITS）段按零填充写入 `*_data.bin`
- 支持 ELF 标准 `.rel.text` / `.rel.data` / `.rela.text` / `.rela.data` 重定位表
  - 可解析符号表中的全局/局部/绝对（`SHN_ABS`）符号
  - 支持常见的 32/16/8 位绝对重定位及 PC 相对重定位（`R_386_32/PC32/16/8` 等）
  - 未定义符号会报错
- 对没有重定位表的旧格式，保留“扫描 32 位绝对地址并修正”的兼容逻辑

## 测试

```sh
make test
# 或
sh tests/run_dlinker_test.sh
```

覆盖旧格式回退、ELF 重定位、`-Ttext`/`-Tdata` 和 `--no-fallback`。
