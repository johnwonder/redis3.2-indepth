/*
** $Id: lparser.h,v 1.57.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"

/*
  第一遍解析源代码并生成AST(Abstract Syntax Tree,抽象语法树)，第二遍再将AST翻译为
  对应的字节码。可以看到，AST仅是分析过程中的中间产物，在实际输出中是不需要的。

  Lua使用一遍扫描(one pass parse) 代码文件的方式生成字节码，即在第一遍扫描代码的
  时候同时就生成字节码了，这么做主要是为了加快解释执行的速度，但是速度快的背后却是
  这一部分代码比较难以理解。

  Lua使用的是递归下降法（recursive descent method)进行解析，这个分析方式针对
  文法中的每一个非终结符（non-terminal symbol),建立一个子程序模拟语法树向下推导，
  在推导过程中遇到终结符(terminal symbol)则检查是否匹配，遇到非终结符则调用
  对应的相关子程序进行处理。
*/

/*
** Expression descriptor
*/

typedef enum {
  VVOID,	/* no value */
  VNIL,
  VTRUE,
  VFALSE,
  VK,		/* info = index of constant in `k' */
  VKNUM,	/* nval = numerical value */
  VLOCAL,	/* info = local register */
  VUPVAL,       /* info = index of upvalue in `upvalues' */
  VGLOBAL,	/* info = index of table; aux = index of global name in `k' */
  VINDEXED,	/* info = table register; aux = index register (or `k') */
  VJMP,		/* info = instruction pc */
  VRELOCABLE,	/* info = instruction pc */
  VNONRELOC,	/* info = result register */
  VCALL,	/* info = instruction pc */
  VVARARG	/* info = instruction pc */
} expkind;


//解析表达式的结果存放在一个临时数据结构expdesc中
typedef struct expdesc {
  expkind k; //具体的类型

  //根据不同的类型存储的数据有所区分，具体看expkind类型定义后的注释
  union {
    struct { int info, aux; } s;
    lua_Number nval;
  } u;
  int t;  /* 跳转指令相关 patch list of `exit when true' */
  int f;  /* patch list of `exit when false' */
} expdesc;


typedef struct upvaldesc {
  lu_byte k;
  lu_byte info;
} upvaldesc;


struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
typedef struct FuncState {
  Proto *f;  /* current function header  保存这个FuncState解析指令之后生成的指令，其中除了自己的，还包括嵌套的子函数的 */
  Table *h;  /* table to find (and reuse) elements in `k' */
  struct FuncState *prev;  /* enclosing function */
  struct LexState *ls;  /* lexical state */
  struct lua_State *L;  /* copy of the Lua state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to `ncode') */
  int lasttarget;   /* `pc' of last `jump target' */
  int jpc;  /* list of pending jumps to `pc' */
  int freereg;  /* first free register 存放的就是当前函数栈的下一个可用位置 */
  int nk;  /* number of elements in `k' */
  int np;  /* number of elements in `p' */
  short nlocvars;  /* number of elements in `locvars' */
  lu_byte nactvar;  /* number of active local variables */
  upvaldesc upvalues[LUAI_MAXUPVALUES];  /* upvalues */
  unsigned short actvar[LUAI_MAXVARS];  /* declared-variable stack */
} FuncState;


LUAI_FUNC Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                            const char *name);


#endif
