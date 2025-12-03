# Redis Makefile
# Copyright (C) 2009 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file
#
# Makefile使用Redis所需的内容以及传递的标准CFLAGS和LDFLAGS组成最终的FINAL_CFLAGS和FINAL_LDFLAGS、
# 然而，当构建依赖时（Jemalloc, Lua, Hiredis，…）CFLAGS和LDFLAGS被传播到依赖项，
# 所以要传递仅在编译/链接Redis本身时使用的标志，使用REDIS_CFLAGS和REDIS_LDFLAGS代替（这是‘make gcov’的情况）
# The Makefile composes the final FINAL_CFLAGS and FINAL_LDFLAGS using
# what is needed for Redis plus the standard CFLAGS and LDFLAGS passed.
# However when building the dependencies (Jemalloc, Lua, Hiredis, ...)
# CFLAGS and LDFLAGS are propagated to the dependencies, so to pass
# flags only to be used when compiling / linking Redis itself REDIS_CFLAGS
# and REDIS_LDFLAGS are used instead (this is the case of 'make gcov').
#
# Dependencies are stored in the Makefile.dep file. To rebuild this file
# Just use 'make dep', but this is only needed by developers.

# $(shell ...) 是Makefile的内置函数，用于执行shell命令
# sh -c './mkreleasehdr.sh' 表示调用shell执行当前目录下的mkreleasehdr.sh脚本
# := 定义时立即展开 值固定
release_hdr := $(shell sh -c './mkreleasehdr.sh')
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
#If you’d like a variable to be set to a value only if it’s not already set, 
#then you can use the shorthand operator ‘?=’ instead of ‘=’. These two settings of the variable ‘FOO’ are identical (see The origin Function):
OPTIMIZATION?=-O2
DEPENDENCY_TARGETS=hiredis linenoise lua geohash-int

# Default settings
# 默认设置
STD=-std=c99 -pedantic -DREDIS_STATIC=''
WARN=-Wall -W
OPT=$(OPTIMIZATION)

PREFIX?=/usr/local
INSTALL_BIN=$(PREFIX)/bin
INSTALL=install

# Default allocator 
ifeq ($(uname_S),Linux)
	MALLOC=jemalloc
else
	MALLOC=libc
endif

# Backwards compatibility for selecting an allocator
ifeq ($(USE_TCMALLOC),yes)
	MALLOC=tcmalloc
endif

ifeq ($(USE_TCMALLOC_MINIMAL),yes)
	MALLOC=tcmalloc_minimal
endif

ifeq ($(USE_JEMALLOC),yes)
	MALLOC=jemalloc
endif

ifeq ($(USE_JEMALLOC),no)
	MALLOC=libc
endif

#https://www.gnu.org/software/make/manual/html_node/Include.html
# 如果你想让make忽略一个不存在或不能重制的makefile，而不提示错误，使用-include指令代替include
#If you want make to simply ignore a makefile which does not exist or cannot be remade, with no error message, use the -include directive instead of include
# Override default settings if possible
-include .make-settings

FINAL_CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS) $(REDIS_CFLAGS) -I../deps/geohash-int
FINAL_LDFLAGS=$(LDFLAGS) $(REDIS_LDFLAGS) $(DEBUG)
FINAL_LIBS=-lm
DEBUG=-g -ggdb

ifeq ($(uname_S),SunOS)
	# SunOS
	INSTALL=cp -pf
	FINAL_CFLAGS+= -D__EXTENSIONS__ -D_XPG6
	FINAL_LIBS+= -ldl -lnsl -lsocket -lresolv -lpthread -lrt
else
ifeq ($(uname_S),Darwin)
	# Darwin
	FINAL_LIBS+= -ldl
else
ifeq ($(uname_S),AIX)
        # AIX
        FINAL_LDFLAGS+= -Wl,-bexpall
        FINAL_LIBS+=-ldl -pthread -lcrypt -lbsd
else
ifeq ($(uname_S),OpenBSD)
	# OpenBSD
	FINAL_LIBS+= -lpthread
else
ifeq ($(uname_S),FreeBSD)
	# FreeBSD
	FINAL_LIBS+= -lpthread
else
	# All the other OSes (notably Linux)
	FINAL_LDFLAGS+= -rdynamic
	FINAL_LIBS+=-ldl -pthread
endif
endif
endif
endif
endif
# Include paths to dependencies
FINAL_CFLAGS+= -I../deps/hiredis -I../deps/linenoise -I../deps/lua/src

#链接tcmalloc库
ifeq ($(MALLOC),tcmalloc)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc
endif

ifeq ($(MALLOC),tcmalloc_minimal)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc_minimal
endif

ifeq ($(MALLOC),jemalloc)
	DEPENDENCY_TARGETS+= jemalloc
	FINAL_CFLAGS+= -DUSE_JEMALLOC -I../deps/jemalloc/include
	FINAL_LIBS+= ../deps/jemalloc/lib/libjemalloc.a
endif

REDIS_CC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
REDIS_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS)
REDIS_INSTALL=$(QUIET_INSTALL)$(INSTALL)

CCCOLOR="\033[34m"
LINKCOLOR="\033[34;1m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
MAKECOLOR="\033[32;1m"
ENDCOLOR="\033[0m"

ifndef V
# $@ 是一个自动变量，它代表当前规则中的目标文件（target）
# 整行命令的效果是：在编译过程中，以蓝色打印“CC”，紧接着以黄色打印正在编译的文件名（$@的值），然后将这条信息发送到标准错误输出。
# 这样做的好处是，即使make的输出被重定向或忽略，编译信息也能以醒目的颜色显示在终端上，方便开发者快速定位。

QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_INSTALL = @printf '    %b %b\n' $(LINKCOLOR)INSTALL$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
endif

REDIS_SERVER_NAME=redis-server
REDIS_SENTINEL_NAME=redis-sentinel
REDIS_SERVER_OBJ=adlist.o quicklist.o ae.o anet.o dict.o server.o sds.o zmalloc.o lzf_c.o lzf_d.o pqsort.o zipmap.o sha1.o ziplist.o release.o networking.o util.o object.o db.o replication.o rdb.o t_string.o t_list.o t_set.o t_zset.o t_hash.o config.o aof.o pubsub.o multi.o debug.o sort.o intset.o syncio.o cluster.o crc16.o endianconv.o slowlog.o scripting.o bio.o rio.o rand.o memtest.o crc64.o bitops.o sentinel.o notify.o setproctitle.o blocked.o hyperloglog.o latency.o sparkline.o redis-check-rdb.o geo.o
REDIS_GEOHASH_OBJ=../deps/geohash-int/geohash.o ../deps/geohash-int/geohash_helper.o
REDIS_CLI_NAME=redis-cli
REDIS_CLI_OBJ=anet.o adlist.o redis-cli.o zmalloc.o release.o anet.o ae.o crc64.o
REDIS_BENCHMARK_NAME=redis-benchmark
REDIS_BENCHMARK_OBJ=ae.o anet.o redis-benchmark.o adlist.o zmalloc.o redis-benchmark.o
REDIS_CHECK_RDB_NAME=redis-check-rdb
REDIS_CHECK_AOF_NAME=redis-check-aof
REDIS_CHECK_AOF_OBJ=redis-check-aof.o

#make的规则是：默认使用第一个不以"."(常用语伪目标)和"%"(常用于模式规则)开头命名的目标作为本条规则编译生成的结果。
all: $(REDIS_SERVER_NAME) $(REDIS_SENTINEL_NAME) $(REDIS_CLI_NAME) $(REDIS_BENCHMARK_NAME) $(REDIS_CHECK_RDB_NAME) $(REDIS_CHECK_AOF_NAME)
	@echo ""
	@echo "Hint: It's a good idea to run 'make test' ;)"
	@echo ""

.PHONY: all

# 使用make dep来生成
# Deps (use make dep to generate this)
include Makefile.dep

# 扫描所有 .c 源文件，分析其中的 #include 指令
# 输出每个目标文件(.o)所依赖的所有头文件(.h)
# 自动排除系统头文件（如 stdio.h、stdlib.h 等），只包含用户自定义的头文件
dep:
	$(REDIS_CC) -MM *.c > Makefile.dep

.PHONY: dep

#$(MAKE) 在 Makefile 中表示 make 程序的名称。它可以用于在 make 进程中再次调用 make，以实现递归构建。

#构建依赖项目
#echo是输出STD 到.make_settings文件中
persist-settings: distclean
	echo STD=$(STD) >> .make-settings
	echo WARN=$(WARN) >> .make-settings
	echo OPT=$(OPT) >> .make-settings
	echo MALLOC=$(MALLOC) >> .make-settings
	echo CFLAGS=$(CFLAGS) >> .make-settings
	echo LDFLAGS=$(LDFLAGS) >> .make-settings
	echo REDIS_CFLAGS=$(REDIS_CFLAGS) >> .make-settings
	echo REDIS_LDFLAGS=$(REDIS_LDFLAGS) >> .make-settings
	echo PREV_FINAL_CFLAGS=$(FINAL_CFLAGS) >> .make-settings
	echo PREV_FINAL_LDFLAGS=$(FINAL_LDFLAGS) >> .make-settings
	-(cd ../deps && $(MAKE) $(DEPENDENCY_TARGETS))

#如果需要定义一个伪目标，我们可以直接使用Makefile语法支持的关键字：.PHONY,这个关键词显式地指定一个伪目标。

#它的使用方式是这样的：
#.PHONY : clean
#clean:
#    -rm main

#如果在Makefile中同时存在一个名为clean的普通目标（即一个实际会生成的文件名）和一个未显式定义为伪目标的clean，
#那么当运行make clean时，make可能会尝试去更新那个名为clean的文件，而不是执行你期望的清理命令。
#这样显式地定义伪目标的好处有两点：
#如果同时存在一个普通目标clean,非显示定义的伪目标将无法执行，显式定义伪目标可以解决这个问题，在出现同名普通目标时，它将覆盖普通目标得以执行，同时make将输出警告信息。
#告诉make这就是一个伪目标，不要试图对其做其他处理，这样可以提高编译效率，减少编译时间。
.PHONY: persist-settings

# 以点开头的目标通常被用作伪目标，但这并不是强制性的。你也可以使用不以点开头的名称来定义伪目标，
# 并通过 .PHONY 声明它们。不过，使用以点开头的名称是一种约定俗成的做法，可以帮助区分伪目标和实际文件目标。
# Prerequisites target

# @touch $@ 静默更新目标文件的时间戳（若文件不存在则创建）
.make-prerequisites:
	@touch $@

#strip函数 https://blog.csdn.net/qq_39695119/article/details/118892746
# Clean everything, persist settings and build dependencies if anything changed
ifneq ($(strip $(PREV_FINAL_CFLAGS)), $(strip $(FINAL_CFLAGS)))
.make-prerequisites: persist-settings
endif

ifneq ($(strip $(PREV_FINAL_LDFLAGS)), $(strip $(FINAL_LDFLAGS)))
.make-prerequisites: persist-settings
endif

#$+ 这个变量很像“$^”，也是所有依赖目标的集合。只是它不去除重复的依赖目标。
#然而，需要注意的是，在实际使用Makefile时，$+ 这个自动变量并不常用，因为大多数情况下，我们不需要在编译命令中重复包含相同的依赖文件。
#更常见的是使用 $，它会自动去除重复的文件名，从而避免潜在的编译问题

#后缀为 .a 的文件在UNIX系统和类似UNIX系统（如Linux和macOS）中代表‌静态链接库文件‌。这种文件类型也被称为归档文件（archive file）。
#.a 文件通常由编译器生成，并包含了一个或多个目标文件（.o 文件），这些目标文件可以被链接到其他程序中

# redis-server
# 只要.c文件被修改过（即它们比对应的.o文件新），make就会自动推导并使用gcc -c命令来重新编译这些.c文件，并生成或更新对应的.o文件。
# 因此，‌当Makefile中的依赖为.o文件时，且这些.o文件依赖于更新的.c源文件，make会自动编译这些.c源文件以生成或更新.o文件‌‌

#由于源文件太多，编译生成的中间目标文件太多，而在链接时需要明显地指出中间目标
#文件名，这对于编译很不方便。所以，我们要给中间目标文件打个包，在 Windows 下这种包叫“库文
#件”（Library File），也就是 .lib 文件，在 UNIX 下，是 Archive File，也就是 .a 文件。

#对 libhiredis.a、liblua.a：静态链接（直接复制代码，和 -static 无关）；
# 对系统库（如 libc，程序默认依赖）：默认用动态链接（优先找 libc.so）—— 除非系统中没有动态库，才会用静态库 libc.a。
$(REDIS_SERVER_NAME): $(REDIS_SERVER_OBJ)
	$(REDIS_LD) -o $@ $^ ../deps/hiredis/libhiredis.a ../deps/lua/src/liblua.a $(REDIS_GEOHASH_OBJ) $(FINAL_LIBS)

# redis-sentinel
$(REDIS_SENTINEL_NAME): $(REDIS_SERVER_NAME)
	$(REDIS_INSTALL) $(REDIS_SERVER_NAME) $(REDIS_SENTINEL_NAME)

# redis-check-rdb
$(REDIS_CHECK_RDB_NAME): $(REDIS_SERVER_NAME)
	$(REDIS_INSTALL) $(REDIS_SERVER_NAME) $(REDIS_CHECK_RDB_NAME)

# redis-cli
#  $(REDIS_CLI_OBJ) 会编译
$(REDIS_CLI_NAME): $(REDIS_CLI_OBJ)
	$(REDIS_LD) -o $@ $^ ../deps/hiredis/libhiredis.a ../deps/linenoise/linenoise.o $(FINAL_LIBS)

# redis-benchmark 依赖  ae.o anet.o redis-benchmark.o adlist.o zmalloc.o redis-benchmark.o
# cc -o redis-benchmark ae.o anet.o redis-benchmark.o adlist.o zmalloc.o redis-benchmark.o
# $^ 就是$(REDIS_BENCHMARK_OBJ)
# $@ 就是$(REDIS_BENCHMARK_NAME)
$(REDIS_BENCHMARK_NAME): $(REDIS_BENCHMARK_OBJ)
	$(REDIS_LD) -o $@ $^ ../deps/hiredis/libhiredis.a $(FINAL_LIBS)

# redis-check-aof
$(REDIS_CHECK_AOF_NAME): $(REDIS_CHECK_AOF_OBJ)
	$(REDIS_LD) -o $@ $^ $(FINAL_LIBS)

#$< 是一个自动变量，它代表当前规则中的第一个依赖文件名
#$，它代表所有的依赖文件
# Because the jemalloc.h header is generated as a part of the jemalloc build,
# building it should complete before building any other object. Instead of
# depending on a single artifact, build all dependencies first.
%.o: %.c .make-prerequisites
	$(REDIS_CC) -c $<

clean:
	rm -rf $(REDIS_SERVER_NAME) $(REDIS_SENTINEL_NAME) $(REDIS_CLI_NAME) $(REDIS_BENCHMARK_NAME) $(REDIS_CHECK_RDB_NAME) $(REDIS_CHECK_AOF_NAME) *.o *.gcda *.gcno *.gcov redis.info lcov-html

.PHONY: clean

# 一定要执行distclean 不然.make-settings会存在
distclean: clean
	-(cd ../deps && $(MAKE) distclean)
	-(rm -f .make-*)

.PHONY: distclean

test: $(REDIS_SERVER_NAME) $(REDIS_CHECK_AOF_NAME)
	@(cd ..; ./runtest)

test-sentinel: $(REDIS_SENTINEL_NAME)
	@(cd ..; ./runtest-sentinel)

check: test

lcov:
	$(MAKE) gcov
	@(set -e; cd ..; ./runtest --clients 1)
	@geninfo -o redis.info .
	@genhtml --legend -o lcov-html redis.info

test-sds: sds.c sds.h
	$(REDIS_CC) sds.c zmalloc.c -DSDS_TEST_MAIN -o /tmp/sds_test
	/tmp/sds_test

.PHONY: lcov

bench: $(REDIS_BENCHMARK_NAME)
	./$(REDIS_BENCHMARK_NAME)

32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"
#又执行了make命令
# -fprofile-arcs 是 GCC/Clang 的 分支覆盖率统计选项 必须和 -ftest-coverage（或 -coverage）一起使用
gcov:
	$(MAKE) REDIS_CFLAGS="-fprofile-arcs -ftest-coverage -DCOVERAGE_TEST" REDIS_LDFLAGS="-fprofile-arcs -ftest-coverage"

noopt:
	$(MAKE) OPTIMIZATION="-O0"

valgrind:
	$(MAKE) OPTIMIZATION="-O0" MALLOC="libc"

src/help.h:
	@../utils/generate-command-help.rb > help.h

# 先创建/usr/local/bin目录
#mkdir -p 是一个在类 Unix 操作系统（如 Linux 和 macOS）中使用的命令，用于创建新的目录。
# 这个命令的 -p 选项意味着 "parents"，它会确保你想要创建的目录的完整路径被创建，即使其中的某些父目录还不存在
install: all
	@mkdir -p $(INSTALL_BIN) 
	$(REDIS_INSTALL) $(REDIS_SERVER_NAME) $(INSTALL_BIN)
	$(REDIS_INSTALL) $(REDIS_BENCHMARK_NAME) $(INSTALL_BIN)
	$(REDIS_INSTALL) $(REDIS_CLI_NAME) $(INSTALL_BIN)
	$(REDIS_INSTALL) $(REDIS_CHECK_RDB_NAME) $(INSTALL_BIN)
	$(REDIS_INSTALL) $(REDIS_CHECK_AOF_NAME) $(INSTALL_BIN)
	@ln -sf $(REDIS_SERVER_NAME) $(INSTALL_BIN)/$(REDIS_SENTINEL_NAME)
