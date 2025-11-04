/*
** $Id: lstate.h,v 2.24.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"

/*
 状态对象

*/


struct lua_longjmp;  /* defined in ldo.c */


/* table of globals */
#define gt(L)	(&L->l_gt)

/* registry */
#define registry(L)	(&G(L)->l_registry)


/* 
  extra stack space to handle TM calls and some other extras
  额外的堆栈空间来处理TM调用和其他一些额外的
 */
#define EXTRA_STACK   5


#define BASIC_CI_SIZE           8

#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/*
  Lua会把系统重的所有字符串存在一个全局的地方，这个全局变量就是
  global_state的strt成员，这是一个散列数组，专门用于存放字符串。

  当新创建一个字符串TString时，首先根据散列算法算出散列值，这就是strt数组的索引值。
  如果这里已经有元素，则使用链表串接起来。

  使用散列桶来存放数据，又有一个问题需要考虑：当数据量非常大时，分配到每个桶上的数据
  也会非常多，这样一次查找也退化成了一次线性的查找过程。Lua中也考虑了这种情况，所以有
  一个重新散列的过程,这就是当字符串数据非常多时，会重新分配桶的数量，降低
  每个桶上分配到的数据数量，这个过程在函数luaS_resize中
*/
typedef struct stringtable {
  GCObject **hash;
  lu_int32 nuse;  /* number of elements */
  int size;
} stringtable;


/*
** informations about a call
   执行函数的基础

   一个被调用的函数再调用其他函数时，会产生新的CallInfo实例。
   此时保存当前被调用函数信息的CallInfo实例的next指针就要指向新的被调用
   函数的CallInfo实例，同时新实例的previous需要指向当前的CallInfo实例
*/
typedef struct CallInfo {
  StkId base;  /* base for this function */
  StkId func;  /* 指明了函数在stack中的位置 function index in the stack */
  StkId	top;  /* 指明被调用函数的栈顶位置 top for this function */
  const Instruction *savedpc;
  int nresults;  /* 被调用函数一共要返回多少个值 expected number of results from this function */
  int tailcalls;  /* number of tail calls lost under this entry */
} CallInfo;



#define curr_func(L)	(clvalue(L->ci->func))
#define ci_func(ci)	(clvalue((ci)->func))
#define f_isLua(ci)	(!ci_func(ci)->c.isC)
#define isLua(ci)	(ttisfunction((ci)->func) && f_isLua(ci))

/*
  要理解lua虚拟机，有两个特别重要的结构要弄清楚，一个是前面说的global_State结构，
  还有一个是Lua虚拟机自定义的线程结构，也被称为lua_State结构。

  Lua虚拟机里的“线程” 和操作系统的线程是有区别的。操作系统中多条线程直接可以并发(分时间片交替运行)

  或并行(在同一时刻 不同cpu核心) 执行，而lua虚拟机的线程 则不行。

  lua虚拟机的线程切换，必须等正在运行的线程 先执行完或者主动调用挂起函数，否则其他线程
  不会被执行。 lua虚拟机的线程 实际上是运行在操作系统的线程内。在实践中，一条操作系统线程，
  在同一时刻往往只会运行lua虚拟机里其中的一条线程。当lua虚拟机内部存在多个线程实例时，
  除了主线程，其他线程实际上是协程。

*/

/*
** `global state', shared by all threads of this state

    全局状态，被这个状态的所有线程共享

    为虚拟机开辟和释放内存所需的内存分配函数，保存GC对象和状态的成员变量，以及一个主线程结构实例 全局注册表等

    开辟内存和释放内存 用户可以自定义分配器,也可以使用官方默认。官方默认会调用realloc和free函数
*/
typedef struct global_State {
  stringtable strt;  /* 短字符串的全局缓存 字符串的hash表 hash table for strings */
  lua_Alloc frealloc;  /*  重新分配内存的函数 function to reallocate memory */
  void *ud;         /* auxiliary data to `frealloc' 当自定义分配器时，可能要用到这个结构 */
  lu_byte currentwhite; //当前新创建的GCObject的颜色。atomic阶段之前是white,atomic阶段之后
  //会被设置为otherwhite.sweep阶段只清理颜色为white的GCObject.


  lu_byte gcstate;  /* 垃圾回收器的状态 开启新一轮GC之后，当前GC所处的阶段(pause,propagate,atomic,sweep等) state of garbage collector */
  int sweepstrgc;  /* position of sweep in `strt' */

  //在sweep阶段会遍历allgc链表，将标记为白色的GCObject释放掉，
  //将标记为黑色的GCObject重新设置为下一轮GC要被清理的白色。
  GCObject *rootgc;  /* 5.3.6改成allgc 可回收对象的列表 list of all collectable objects */
  GCObject **sweepgc;  /*记录当前要被清理或重新标记的GCObject position of sweep in `rootgc' */
  GCObject *gray;  /*被标记为灰色的GCObject 会被放入gray链表 list of gray objects */
  GCObject *grayagain;  /*被标记为黑色的GCObject重新被设置为灰色时，要放入grayagain链表，lua表通常会有这种操作 list of objects to be traversed atomically */
  GCObject *weak;  /* list of weak tables (to be cleared) */
  GCObject *tmudata;  /* last element of list of userdata to be GC */


  Mbuffer buff;  /* temporary buffer for string concatentation */
  lu_mem GCthreshold;
  lu_mem totalbytes;  /* number of bytes currently allocated */
  lu_mem estimate;  /* an estimate of number of bytes actually in use */
  lu_mem gcdept;  /* how much GC is `behind schedule' */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC `granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  //全局注册表 本质上是一个lua表对象，在全局注册表Registry中有只有数组被用到，
  //并且第一个值是指向“主线程”的指针，而第二个值是指向全局表（也就是_G）的指针
  TValue l_registry; 
  //主线程结构实例 指向“主线程”结构的指针
  //在Lua C层代码中，可以很轻易的拿到global_state指针，而这个MainThread指针
  //可以方便的获取MainThread对象
  struct lua_State *mainthread; 
  UpVal uvhead;  /* head of double-linked list of all open upvalues */
  struct Table *mt[NUM_TAGS];  /* metatables for basic types */
  TString *tmname[TM_N];  /* array with tag-method names */
} global_State;


/*
** `per thread' state

    《Lua解释器构建》
    Lua虚拟机的“线程”结构实际上是lua_State结构。
    GC相关：
    Stack相关：每个Lua线程实例，都会有自己独立的栈空间，信息等。
    这些部分包含在stack相关的域内，Lua的函数会在栈上执行，临时变量
    也会暂存在栈上，同时栈的起始地址，大小信息也包含在里面。
    此外，虚拟寄存器也会直接使用栈上的空间。
    status:代表了lua线程实例的状态，lua线程在初始化阶段会被设置为lua_ok.
    global_state: 执行虚拟机中global_state结构的指针
    Callinfo:函数调用相关的信息。函数(lua函数和C函数)要被执行，
    首选函数实体要被压入lua_State结构的栈中，然后再进行调用。
    CallInfo相关的信息则回记录被调用的函数在栈中的位置。
    每个被调用的函数都会有自己独立的虚拟栈(lua_state栈中的某个片段)
    CallInfo信息会记录独立虚拟栈的栈顶信息。
    此外，它还记录了被调用的函数有几个返回值，调用的状态以及当前执行的指令地址等。
    是和lua栈一样具有同等重要性的数据结构。
    异常处理相关：当lua栈内函数调用发生异常时，需要这些异常相关的变量协助进行错误处理。



    《lua设计与实现》
    每一个lua虚拟机对应一个lua_State结构体，它使用TValue数组来模拟栈，
    其中包括几个与栈相关的成员。
    stack,base top

    这些成员的初始化操作在stack_init函数中完成。

    然而lua_State里面存放的是一个Lua虚拟机的全局状态，
    当执行到一个函数时，需要有对应的数据结构来表示函数相关的信息。
    这个数据结构就是CallInfo,这个结构体中同样有top,base这两个与栈有关的成员。

    无论函数怎么执行，有多少函数，最终他们引用到的栈都是当前Lua虚拟机的栈。

    《Lua解释器构建》
    。stack指针到top指针的部分就是调⽤函数时的虚拟机栈。读者可以看到，调
      ⽤某个函数的虚拟机栈是stack上的⼀个部分。虚拟机栈两边的数字就是虚拟机栈索引，同样的
      空间可以⽤正值和负值去索引。
*/
struct lua_State {
  CommonHeader;
  lu_byte status; //代表了lua线程实例的状态，lua线程在初始化阶段会被设置为lua_ok.
  StkId top;  /* first free slot in the stack 表示当前被调用的函数 虚拟机的栈顶位置（栈底是被调用函数所在位置的下一个位置） 当前栈的下一个可用位置  栈顶，调用函数时动态改变*/
  StkId base;  /* base of current function 当前函数栈的基地址 */
  global_State *l_G; //为虚拟机开辟和释放内存所需的内存分配函数，保存GC对象和状态的成员变量，以及一个主线程结构实例 全局注册表等
  
  /*当前被调用函数的信息*/
  CallInfo *ci;  /* call info for current function  当前运行的  指向当前函数的CallInfo指针 */
  const Instruction *savedpc;  /* `savedpc' of current function */
  StkId stack_last;  /* 从这里开始，栈不能被使用 last free slot in the stack */
  StkId stack;  /* stack base 栈数组的起始位置  用来暂存虚拟机运行过程中的临时变量 */
  CallInfo *end_ci;  /* points after end of ci array*/
  /* 
    和lua_state生命周期一致的函数调用信息 array of CallInfo's 
    和lua虚拟机线程类型 实例生命周期保持一致的基础函数调用信息
  */
  CallInfo *base_ci;  
  int stacksize; //栈的整体大小
  int size_ci;  /* size of array `base_ci' */
  unsigned short nCcalls;  /* 进行多少次函数调用 number of nested C calls */
  unsigned short baseCcalls;  /* nested C calls when resuming coroutine */
  lu_byte hookmask;
  lu_byte allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  TValue l_gt;  /* table of globals 使用TValue数组来模拟栈 */
  TValue env;  /* temporary place for environments */
  GCObject *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  //保护模式下，当异常抛出时，跳出逻辑
  struct lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* 错误函数位于栈的哪个位置 current error handling function (stack index) */
};


#define G(L)	(L->l_G)


/*

  5.3.6改成了GCUnion 而GCHeader改成了 GCObject  
** Union of all collectable objects

   将所有需要进行垃圾回收的数据类型囊括了

    typedef struct GCheader {
    CommonHeader;
  } GCheader;
*/
union GCObject {
  GCheader gch;
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;  /* thread */
};


/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)	check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2cl(o)	check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o)	check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)	check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)	check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define ngcotouv(o) \
	check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)	check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

/* macro to convert any Lua object into a GCObject */
#define obj2gco(v)	(cast(GCObject *, (v)))


LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);

#endif

