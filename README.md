# DOCTOR Development Suite

DOCTOR 是一个低端教学/实验用指令集架构。本仓库包含：

- **doctor-sim**：DOCTOR 模拟器
  - `src/`：模拟器源码
  - `tests/`：汇编/模拟器测试
  - `build/`：构建产物
  - `Makefile` / `README.md` / `dev_specification.md`
- **dasm**：DOCTOR 汇编器
  - 支持生成普通二进制或 ELF32 relocatable `.o`
- **dcc**：DOCTOR C 子集编译器（C99 重写版）
  - 支持输出 dasm 汇编、ELF32 目标文件、二进制
  - 提供 `lib/`、`docs/`、`tests/`
- **dlinker**：DOCTOR 最小链接器（C99 重写版）
  - 链接多个 ELF32 `.o`，生成 `*_code.bin` / `*_data.bin`
- **dda**：DOCTOR ELF 反汇编器
  - 将 ELF32 `.o` 反汇编为 DASM 汇编文本
- `manual.md`：DOCTOR 指令集架构手册

## 目录结构

```
.
├── Makefile
├── run_tests.sh
├── README.md
├── TODO.md
├── manual.md
├── doctor-sim/
│   ├── dev_specification.md
│   ├── Makefile
│   ├── README.md
│   ├── src/
│   ├── tests/
│   └── build/
├── dcc/
│   ├── Makefile
│   ├── README.md
│   ├── src/
│   ├── lib/
│   ├── docs/
│   └── tests/
├── dasm/
│   ├── Makefile
│   ├── README.md
│   ├── src/
│   ├── build/
│   └── tests/
├── dlinker/
│   ├── Makefile
│   ├── README.md
│   ├── src/
│   └── tests/
└── dda/
    ├── Makefile
    ├── README.md
    └── src/
```

## 快速构建

```sh
# 一键构建全部子项目（doctor-sim / dasm / dcc / dlinker / dda）
make

# 也可以单独构建某个子项目
make -C doctor-sim
make -C dasm
make -C dcc
make -C dlinker
make -C dda

# 清理全部构建产物
make clean

# 运行各子项目测试
make test

# 或直接运行一键测试脚本
./run_tests.sh
```

## 简单工具链示例

```sh
# C → ELF 目标文件
dcc/dcc -m elf main.c lib.c main.o

# 链接 ELF 目标文件 → 二进制
dlinker/dlinker -o main main.o

# 运行模拟器
doctor-sim/build/bin/doctor-sim -f code main_code.bin data main_data.bin
```

各子项目的详细说明见各自目录下的 `README.md`。
