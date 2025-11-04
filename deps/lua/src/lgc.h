/*
** $Id: lgc.h,v 2.15.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"

/*
 增量式垃圾收集器

 pause阶段，在Lua中，包含stack变量的mainthread和global_state都是要被gc关注的
   对象。 GC的起始点是全局变量，栈和寄存器。寄存器是在Lua的栈中模拟的，并没有特别的意义
   因为所有的操作都是在栈桑进行的。因为mainthread和global table包含GC起始点，
   因此它们要先插入到gray链表中，并且标记它们为灰色。这些操作结束后，就进入propagate阶段。

   propagate阶段：不断从gray链表中取出object,然后把它从灰色变为黑色，再遍历它所有引用的对象，
   并将其插入到gray链表中。 每次将一个gray链表中取出来的object标记为黑色后，需要累积这个object
   的字节大小。当这一次gc步骤扫描的对象累积超过一定的字节数时，本轮GC步骤会被终止，
   等待下一次GC步骤开始后，继续扫描gray链表中的对象。
   当gray链表为空，意味着所有的gray链表均已扫描完毕，然后进入atomic阶段。
*/
/*
** Possible states of the Garbage Collector
*/
#define GCSpause	0
#define GCSpropagate	1
#define GCSsweepstring	2
#define GCSsweep	3
#define GCSfinalize	4


/*
** some userful bit tricks
*/
#define resetbits(x,m)	((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)	((x) |= (m))
#define testbits(x,m)	((x) & (m))
#define bitmask(b)	(1<<(b))
#define bit2mask(b1,b2)	(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)	setbits(x, bitmask(b))
#define resetbit(x,b)	resetbits(x, bitmask(b))
#define testbit(x,b)	testbits(x, bitmask(b))
#define set2bits(x,b1,b2)	setbits(x, (bit2mask(b1, b2)))
#define reset2bits(x,b1,b2)	resetbits(x, (bit2mask(b1, b2)))
#define test2bits(x,b1,b2)	testbits(x, (bit2mask(b1, b2)))



/*
** Layout for bit use in `marked' field:
** bit 0 - object is white (type 0)
** bit 1 - object is white (type 1)
** bit 2 - object is black
** bit 3 - for userdata: has been finalized
** bit 3 - for tables: has weak keys
** bit 4 - for tables: has weak values
** bit 5 - object is fixed (should not be collected)
** bit 6 - object is "super" fixed (only the main thread)
*/


#define WHITE0BIT	0
#define WHITE1BIT	1
#define BLACKBIT	2
#define FINALIZEDBIT	3
#define KEYWEAKBIT	3
#define VALUEWEAKBIT	4
#define FIXEDBIT	5
#define SFIXEDBIT	6
#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


#define iswhite(x)      test2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define isblack(x)      testbit((x)->gch.marked, BLACKBIT)
#define isgray(x)	(!isblack(x) && !iswhite(x))

#define otherwhite(g)	(g->currentwhite ^ WHITEBITS)
#define isdead(g,v)	((v)->gch.marked & otherwhite(g) & WHITEBITS)

#define changewhite(x)	((x)->gch.marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->gch.marked, BLACKBIT)

#define valiswhite(x)	(iscollectable(x) && iswhite(gcvalue(x)))

#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)


#define luaC_checkGC(L) { \
  condhardstacktests(luaD_reallocstack(L, L->stacksize - EXTRA_STACK - 1)); \
  if (G(L)->totalbytes >= G(L)->GCthreshold) \
	luaC_step(L); }


#define luaC_barrier(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	luaC_barrierf(L,obj2gco(p),gcvalue(v)); }

#define luaC_barriert(L,t,v) { if (valiswhite(v) && isblack(obj2gco(t)))  \
	luaC_barrierback(L,t); }

#define luaC_objbarrier(L,p,o)  \
	{ if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) \
		luaC_barrierf(L,obj2gco(p),obj2gco(o)); }

#define luaC_objbarriert(L,t,o)  \
   { if (iswhite(obj2gco(o)) && isblack(obj2gco(t))) luaC_barrierback(L,t); }

LUAI_FUNC size_t luaC_separateudata (lua_State *L, int all);
LUAI_FUNC void luaC_callGCTM (lua_State *L);
LUAI_FUNC void luaC_freeall (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_fullgc (lua_State *L);
LUAI_FUNC void luaC_link (lua_State *L, GCObject *o, lu_byte tt);
LUAI_FUNC void luaC_linkupval (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_barrierf (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback (lua_State *L, Table *t);


#endif
