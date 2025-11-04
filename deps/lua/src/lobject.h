/*
** $Id: lobject.h,v 2.20.1.2 2008/08/06 13:29:48 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/* tags for values visible from Lua */
#define LAST_TAG	LUA_TTHREAD

#define NUM_TAGS	(LAST_TAG+1)


/*

 标记值和对象表示。先浏览一下这个。你会想要一直打开一个窗口来保存这个文件。
*/

/*
** Extra tags for non-values
*/
#define LUA_TPROTO	(LAST_TAG+1)
#define LUA_TUPVAL	(LAST_TAG+2)
#define LUA_TDEADKEY	(LAST_TAG+3)


/*
** Union of all collectable objects
*/
typedef union GCObject GCObject;


/*
 所有可收集对象的通用头（以宏形式），将包含在其他对象中
** Common Header for all collectable objects (in macro form, to be
** included in other objects)

  next:指向下一个GC链表的成员，
  tt: 表示数据的类型，即前面的那些表示数据类型的宏。
  marked: GC相关的标记位，

  这些类型包括字符串、表、函数、线程、⽤户数据等。marked变量则表示该GCObject的颜⾊
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common header in struct form
*/
typedef struct GCheader {
  CommonHeader;
} GCheader;

/*
 5.3.6版本是这样的
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked
 
struct GCObject {
  CommonHeader;
};

//5.3 lstate.h中
union GCUnion {
  GCObject gc;   
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  
};
//5.1
union GCObject {
  GCheader gch;
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;  
};
*/



/*
** Union of all Lua values

   所有lua值的联合体
*/
typedef union {
  //当是可回收的类型时，该对象的指针将存储在gc变量中。
  //可以被lua GC机制回收的类型，包括字符串类型，表类型，函数类型
  //函数类型有三种：一种是Light C Function，另一种是C Closure（C闭包);
  //还有一种是Lua Closure（Lua闭包）。后两种可以被Lua GC机制回收）。
  //userdata类型 和线程类型。
  GCObject *gc; /* 可被垃圾回收的对象   */
  void *p; /* light userdata类型对象 使用者自行释放 */
  lua_Number n;  /* 浮点型变量 */
  int b; /* 布尔类型的值 */
  /*
     5.3多了
     //只有一个参数，就是Lua虚拟机的线程类型实例，它所有的参数都在线程的栈总。
     //只有一个int类型的返回值，这个返回值告诉调用者，在lua_CFunction函数
     被调用完成之后，有多少个返回值还在栈中，
     lua_CFunction f;  存放的函数指针。和另两类函数不一样，它没有上值列表，
     
     lua_Integer i;   
  
  */
} Value;


/*
** Tagged Values

   Lua的基本类型包括：nil类型，布尔类型，轻量用户数据类型，
   字符串类型，表类型，函数类型，完全用户数据类型和线程类型，
   lua通过一个通用类型来表示所有类型的数据
*/

#define TValuefields	Value value; int tt

typedef struct lua_TValue {
  TValuefields;//表示类型的tt ,表示值的value
} TValue;


/* Macros to test type */
#define ttisnil(o)	(ttype(o) == LUA_TNIL)
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)
#define ttistable(o)	(ttype(o) == LUA_TTABLE)
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA)

/* Macros to access values */
#define ttype(o)	((o)->tt)
#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o)	check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)	check_exp(ttisthread(o), &(o)->value.gc->th)

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/*
** for internal debug only
*/
#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))


/* Macros to set values */
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
    checkliveness(G(L),i_o); }

#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
    checkliveness(G(L),i_o); }

#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }

#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

#define setptvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; \
    checkliveness(G(L),i_o); }




#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }


/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

#define setttype(obj, tt) (ttype(obj) = (tt))

/* 
  使用一个宏来表示哪些数据类型需要进行GC
  可以看到 ,LUA_TSTRING 包括LUA_TSTRING 之后的数据类型都需要进行GC操作。

  这些需要进行GC操作的数据类型都会有一个CommonHeader宏定义的成员，
  并且这个成员在结构体定义的最开始部分。
 */
#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)



typedef TValue *StkId;  /* 栈元素的索引 index to stack elements */


/*
** String headers for string table

   为了让TString数据类型按照L_Umaxalign 类型来对齐

   在C语言中，struct/union这样的复合数据类型是按照这个类型中最大对齐量的数据来对齐的，
   所以这里就是按照double类型的对齐量来对齐的，一般而言是8字节。之所以要进行对齐操作，
   是为了在CPU读取数据时性能更高。
*/
typedef union TString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  struct {
    CommonHeader;
    lu_byte reserved; //这个变量用于标示这个字符串是否是Lua虚拟机中的保留字符串。如果这个值为1，那么将不会在GC阶段被回收，而是一直保留在系统中。只有Lua语言中的关键字才会是保留字符串。
    unsigned int hash;//该字符串的散列值。Lua的字符串比较并不会像一般的做法那样进行逐位对比，而是仅比较字符串的散列值。
    size_t len; //字符串长度。
  } tsv;
} TString;


#define getstr(ts)	cast(const char *, (ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))



typedef union Udata {
  L_Umaxalign dummy;  /* ensures maximum alignment for `local' udata */
  struct {
    CommonHeader;
    struct Table *metatable;
    struct Table *env;
    size_t len;
  } uv;
} Udata;


/*
  指令执行的部分 解释器分析lua文件之后生成Proto结构体，
  最后到luaV_execute函数中依次取出指令来执行。

  而内存部分，在lua解释器中就存放在lua栈中。lua中也是把栈的
  某一个位置称为寄存器， 这里的寄存器并不是cpu中的寄存器

  每一个lua虚拟机对应一个lua_State结构体，它使用TValue数组来模拟栈，

*/


/*
将字节码信息存在⼀个被叫作Proto的内存结构中，这个结构
主要是存放编译结果（指令、常量等信息）。虚拟机在运⾏的过程中，会从Proto结构中取出⼀
个个的字节码，然后再执⾏。

** Function Prototypes

    函数的常量数组
    编译生成的字节码信息，也就是前面提到的code成员。
    函数的局部变量信息。
    保存upvalue名字的数组。

    Instruction *code 指令列表 存放编译好的指令。 最后一个指令通常是RETURN指令，代表程序结束
*/
typedef struct Proto {
  CommonHeader;
  TValue *k;  /* 脚本中的常量 constants used by the function */
  Instruction *code; //生成的OpCode存放在Proto结构体的code数组中 指令列表 存放编译好的指令。
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines */
  struct LocVar *locvars;  /* information about local variables 函数的所有局部变量信息。 */
  TString **upvalues;  /* upvalue names */
  TString  *source;
  int sizeupvalues;
  int sizek;  /* size of `k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of `p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  GCObject *gclist;
  lu_byte nups;  /* number of upvalues */
  lu_byte numparams;
  lu_byte is_vararg;
  lu_byte maxstacksize;
} Proto;


/* masks for new-style vararg */
#define VARARG_HASARG		1
#define VARARG_ISVARARG		2
#define VARARG_NEEDSARG		4

//函数的所有局部变量的LocVar信息，一般存放在Proto结构体的locvars中
typedef struct LocVar {
  TString *varname; //变量名
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;



/*
** Upvalues
*/

typedef struct UpVal {
  CommonHeader;
  TValue *v;  /* points to stack or to its own value */
  union {
    TValue value;  /* the value (when closed) */
    struct {  /* double linked list (when open) */
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
	struct Table *env

typedef struct CClosure {
  ClosureHeader;
   //只有一个参数，就是Lua虚拟机的线程类型实例，它所有的参数都在线程的栈总。
   //只有一个int类型的返回值，这个返回值告诉调用者，在lua_CFunction函数
  //被调用完成之后，有多少个返回值还在栈中，
  //和另两类函数不一样，它没有上值列表，
  lua_CFunction f;
  TValue upvalue[1];
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)


/*
** Tables

   Lua表中将数据存放在两种类型的数据结构中，一个是数组，一个是散列表。

   如果输入的key是一个正整数，并且它的值>0 

*/

typedef union TKey {
  struct {
    TValuefields;
    struct Node *next;  /* for chaining */
  } nk;
  TValue tvk;
} TKey;


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


/*
  使用表来统一表示lua中的一切数据，是lua区分于其他语言的一个特色。
  这个特色从最开始的lua版本保持至今，很大的原因是为了在设计上保持简洁。
  lua表分为数组和散列表部分，其中数组部分不像其他语言那样，从0开始作为第一个索引，而是
  从1开始。散列表部分可以存储任何其他不能存放在数组部分的数据，唯一的要求就是键值不能为nil。
  尽管内部实现上区分了这两个部分，但是对使用者而言却是透明的。使用lua表，可以模拟出其他各种
  数据结构--数组，链表，树等。

*/

typedef struct Table {
  CommonHeader;
  /*
     这个byte类型的数据，用于标示这个表中提供了哪些元方法。
     最开始这个flags为空，也就是0，当查找一次后，如果该表中
     存在某个元方法，那么将该元方法对应的flag bit设为1，这样下一次
     查找时只需要比较这个bit就行了。每个方法对应的bit定义在ltm.h文件中。
   */
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present 表示 标记方法(p) 不存在*/ 
  /*
    该表中以2为底的散列表大小的对数值。同时由此可知，散列表部分
    的大小一定是2的幂，即如果散列通数组要扩展的话，也是以每次在原大小
    基础上乘以2的形式扩展。

  */
 
     /*
    由于在散列桶部分，每个散列值相同的数据都会以链表的形式串起来，
    所以即使数量用完了，也不要紧因此这里使用byte类型，而且是原数据以2为底的对数值，
    因为要根据这个值还原回原来的真实数据，也只是需要移位操作罢了，速度很快。
  */
  lu_byte lsizenode;  /* log2 of size of `node' array */
  /*
     存放该表的元表
  */
  struct Table *metatable;
  /*
     指向数组部分的指针
  */
  TValue *array;  /* array part */

  /*
     指向该表的散列桶数组起始位置的指针。
  */
  Node *node;
  /*
     指向该表散列通数组的最后空闲位置的指针
  */
  Node *lastfree;  /* any free position is before this position */
  /*
     gc相关的链表
  */
  GCObject *gclist;
  /*
    数组部分的大小。
  */
  int sizearray;  /* size of `array' array */

 
} Table;



/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x)	(luaO_log2((x)-1) + 1)

LUAI_FUNC int luaO_log2 (unsigned int x);
LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

