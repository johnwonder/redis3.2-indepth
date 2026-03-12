#!/bin/sh



GIT_SHA1=`(git show-ref --head --hash=8 2> /dev/null || echo 00000000) | head -n1`

# 生成 Git 工作区 / 暂存区的修改差异，屏蔽无关输出
# wc -l 统计输入内容的行数（line count）
GIT_DIRTY=`git diff --no-ext-diff 2> /dev/null | wc -l`
BUILD_ID=`uname -n`"-"`date +%s`

# 测试release.h文件是否存在 或者创建release.h文件
# test左边如果文件存在 则返回0
test -f release.h || touch release.h
(cat release.h | grep SHA1 | grep $GIT_SHA1) && \
(cat release.h | grep DIRTY | grep $GIT_DIRTY) && exit 0 # Already up-to-date
echo "#define REDIS_GIT_SHA1 \"$GIT_SHA1\"" > release.h
echo "#define REDIS_GIT_DIRTY \"$GIT_DIRTY\"" >> release.h
echo "#define REDIS_BUILD_ID \"$BUILD_ID\"" >> release.h
touch release.c # Force recompile of release.c
# touch若文件不存在，则创建空文件；若存在，则更新时间戳为当前时间
# 更新时间后 会强制重新编译
