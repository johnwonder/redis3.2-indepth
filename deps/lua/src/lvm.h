/*
** $Id: lvm.h,v 2.5.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include "ldo.h"
#include "lobject.h"
#include "ltm.h"



/*
  <<Lua设计与实现>>
  一个语言的虚拟机需要完成以下工作。
  将源代码编译成虚拟机可以识别执行的字节码。
  为函数调用准备调用栈
  内部维持一个IP(Instruction Pointer,指令指针)来保存下一个将执行的指令地址。
  在Lua中，IP对应的是PC指针。

  模拟一个CPU的运行：循环拿出由IP指向的字节码，根据字节码格式进行解码，然后执行字节码。
*/

/*
  这个模块主要是用于循环对并分解指令，
  容纳后根据其操作码的枚举值 进行处理或跳转到Lua的其他模块。
  
  设计一套字节码，分析源代码文件生成字节码。
  在虚拟机中执行字节码。
  如何在整个执行过程中保存整个运行环境。
*/

/*

  Lua首先将源程序编译成为字节码，然后交由虚拟机解释执行。
  对于每一个函数，Lua的编译器将创建一个原型(ProtoType)

  脚本语言通常都是解释执行的。每一个脚本语言都有自己定义的OpCode

  一般的编译语言，比如c,经过编译器编译后，生成的都是与当前硬件环境相匹配的汇编代码，
  而脚本型语言经过编译器前端处理之后，生成的都是字节码，再将该字节码放在这门语言的
  虚拟机中逐个执行。



*/


#define tostring(L,o) ((ttype(o) == LUA_TSTRING) || (luaV_tostring(L, o)))

#define tonumber(o,n)	(ttype(o) == LUA_TNUMBER || \
                         (((o) = luaV_tonumber(o,n)) != NULL))

#define equalobj(L,o1,o2) \
	(ttype(o1) == ttype(o2) && luaV_equalval(L, o1, o2))


LUAI_FUNC int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r);
LUAI_FUNC int luaV_equalval (lua_State *L, const TValue *t1, const TValue *t2);
LUAI_FUNC const TValue *luaV_tonumber (const TValue *obj, TValue *n);
LUAI_FUNC int luaV_tostring (lua_State *L, StkId obj);
LUAI_FUNC void luaV_gettable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void luaV_settable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void luaV_execute (lua_State *L, int nexeccalls);
LUAI_FUNC void luaV_concat (lua_State *L, int total, int last);

#endif
