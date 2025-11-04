/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


//Lua内部采用一种通用的基础数据结构来表示所有数据类型。
//Lua语言极其精简，只有字符串和表这两种最基本的数据结构。
// 然而精简并不代表简陋，在这些基础数据结构的实现中，处处可以看到设计者为了性能
// 和可扩展性等所做的努力。


//Lua 是一门动态类型的脚本语言，这意味着同一个变量在不同时刻指向不同类型的数据。
//Lua中，我们使用一个通用的数据结构lua_TValue来统一表示所有在Lua虚拟机中需要保存的
//的数据类型，

// 字符串操作

void luaS_resize (lua_State *L, int newsize) {
  GCObject **newhash;
  stringtable *tb;
  int i;
  //如果当前GC处在回收字符串数据的阶段，那么这个函数直接返回，不进行重新散列的操作。
  if (G(L)->gcstate == GCSsweepstring)
    return;  /* cannot resize during GC traverse */

  //重新分配一个散列桶，并且清空。
  newhash = luaM_newvector(L, newsize, GCObject *);
  tb = &G(L)->strt;
  for (i=0; i<newsize; i++) newhash[i] = NULL;

  /* 遍历原先的数据，存入新的散列桶中， 释放旧的散列桶，保存新分配的散列桶数据。 */
  /* 
    lgc.c 的checkSizes函数，
    lstring.c的newlstr函数：如果此时字符串的数量大于桶数组的数量，
    且桶数组的数量小于MAX_INT/2,那么就进行翻倍的扩容。
  */
  /* rehash */
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
    while (p) {  /* for each node in the list */
      GCObject *next = p->gch.next;  /* save next */
      unsigned int h = gco2ts(p)->hash;
      //新的位置
      int h1 = lmod(h, newsize);  /* new position */
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->gch.next = newhash[h1];  /* chain it */
      newhash[h1] = p;
      p = next;
    }
  }
  luaM_freearray(L, tb->hash, tb->size, TString *);
  tb->size = newsize;
  tb->hash = newhash;
}


static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  TString *ts;
  stringtable *tb;
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  ts = cast(TString *, luaM_malloc(L, (l+1)*sizeof(char)+sizeof(TString)));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.marked = luaC_white(G(L));
  ts->tsv.tt = LUA_TSTRING;
  ts->tsv.reserved = 0;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  tb = &G(L)->strt;
  h = lmod(h, tb->size);
  ts->tsv.next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = obj2gco(ts);
  tb->nuse++;
  if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  return ts;
}

/*
  
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);  /* seed */

  //计算散列值操作时的步长。这一步的初衷是为了在字符串非常大的时候，
  //不需要逐位来进行散列值的计算，而仅需要每步长单位取一个字符就可以了。
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;

  //计算需要新创建的字符串对应的散列值
  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));

  //根据散列值找到对应的散列桶，遍历该散列桶的所有元素，
  //如果能够找到同样的字符串，说明之前已经存在相同字符串，
  //此时不需要重新分配一个新的字符串数据，直接返回即可。
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
    TString *ts = rawgco2ts(o);
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      /* string may be dead */
      //判断这个字符串是否在当前GC阶段被判定为需要回收，
      //如果是，则调用changewrite函数修改它的状态，将其改为不需要进行回收，
      //从而达到复用字符串的目的。
      if (isdead(G(L), o)) changewhite(o);
      return ts;
    }
  }
  //没找到相同的字符串，调用newlstr函数创建一个新的字符串
  return newlstr(L, str, l, h);  /* not found */
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
  u->uv.marked = luaC_white(G(L));  /* is not finalized */
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  /* chain it on udata list (after main thread) */
  u->uv.next = G(L)->mainthread->next;
  G(L)->mainthread->next = obj2gco(u);
  return u;
}

