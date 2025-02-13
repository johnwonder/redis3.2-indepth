# Top level makefile, the real shit is at src/Makefile
#执行 make命令时，首先 会找default ，执行all 目标 ，但是all没找到 所以 会执行.DEFAULT伪目标，
# 这时候 $@就等于all 所以 实际上就是 执行 cd src && make all
# 进入src 目录 执行make all
default: all
#在Makefile中，.DEFAULT是一个特殊的伪目标（pseudo-target），
#它不是一个真正的文件名，而是Makefile内建的一个规则，用于定义当Make无法找到某个目标的构建规则时所执行的操作。
.DEFAULT:
	cd src && $(MAKE) $@

install:
	cd src && $(MAKE) $@

.PHONY: install
