# Brainfuck 解释器

该解释器使用当前仓库的 `ccompiler` 编译核心逻辑，仅将 libc 相关的
文件读取、字符输入输出和错误输出放在 clang/gcc 编译的包装层中。

## 构建与运行

先构建编译器：

```bash
cmake --build build -j2
```

再构建解释器：

```bash
python3 examples/brainfuck/build.py
```

运行 Brainfuck 程序：

```bash
build/brainfuck/brainfuck examples/brainfuck/tests/hello.bf
printf 'abc' | build/brainfuck/brainfuck examples/brainfuck/tests/echo.bf
```

也可以指定包装层使用的 C 编译器：

```bash
HOST_CC=gcc python3 examples/brainfuck/build.py
```

## 编译边界

`main.c` 完全由 `build/ccompiler` 编译，其中包含：

- `main(argc, argv)`、命令行检查和 `.bf` 文件路径选择；
- Brainfuck 指令执行状态机；
- `while` 主循环、`switch` 指令分派和嵌套括号跳转；
- 30000 个单元的纸带及初始化；
- 括号验证、指针边界检查和退出码。

`host.c` 只由 clang/gcc 编译，向核心提供以下包装函数：

```c
char *bf_host_read_file(char *path);
void bf_host_release(char *data);
int bf_host_getchar(void);
void bf_host_putchar(int c);
void bf_host_error(int error_code);
```

这些函数分别封装 `fopen/fread/fclose`、内存分配释放、
`getchar/putchar` 和错误信息输出。`host.c` 不包含 `main`、Brainfuck
解析或执行逻辑。

构建流水线如下：

```text
build/ccompiler main.c -o main.ll
llvm-as main.ll -o main.bc
llc main.bc -relocation-model=pic -filetype=obj -o main.o
clang/gcc -c host.c -o host.o
clang/gcc main.o host.o brainfuck
```

`llvm-as` 和 `llc` 只验证并转换 `ccompiler` 生成的 IR；链接器由
clang/gcc 调用，核心 C 源码不会交给 clang/gcc 重新编译。

## 执行语义

- 纸带包含 30000 个单元，数据指针初始位于第 0 个单元。
- 单元按 8 位无符号整数回绕。
- 非 Brainfuck 指令字符会被忽略。
- 输入到达 EOF 时，`,` 写入 0。
- 未匹配括号和数据指针越界会输出诊断并返回非零退出码。

## 测试

```bash
python3 examples/brainfuck/test.py
HOST_CC=gcc python3 examples/brainfuck/test.py
```

测试覆盖 Hello World、输入回显和 EOF、嵌套循环、括号错误、
双向指针越界及单元回绕。
