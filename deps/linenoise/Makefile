STD=
WARN= -Wall
OPT= -Os

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

linenoise.o: linenoise.h linenoise.c

linenoise_example: linenoise.o example.o
	$(R_LD) -o $@ $^

# 在Makefile中，.c.o: 是一个模式规则（pattern rule），它告诉Make程序如何将一个以 .c 结尾的文件编译成一个以 .o 结尾的目标文件。这里的 $(R_CC) 是一个变量，它应该在此之前被定义，并包含了你想要使用的C编译器的命令（比如 gcc 或 clang）。-c 是编译器的选项，表示只编译源文件而不进行链接。$< 是一个自动变量，它代表规则中的第一个依赖文件，也就是对应的 .c 源文件。
# 整个规则 .c.o: 可以解释为：“对于任何想要从 .c 文件编译成 .o 文件的目标，使用 $(R_CC) 编译器加上 -c 选项来编译源文件。
.c.o:
	$(R_CC) -c $<

clean:
	rm -f linenoise_example *.o
