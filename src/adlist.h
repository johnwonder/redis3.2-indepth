/* adlist.h - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ADLIST_H__
#define __ADLIST_H__

/*
    ■ 3.2 版本后开始使用 quicklist (#2143) 代替了 linkedlist 和 ziplist，不过后面讲解的部分还会是双端链表，代码虽然会有点点老，但是思想还是需要了解的
    ■ redis 列表实现目前只为快速列表，快速链表实际上是 双端链表 + 压缩列表的集合体，即它本身是个双端链表，每个链表节点是一个压缩列表
    ■ 7.0 版本里，ziplist 压缩列表已经被废弃了，用 listpack 进行了全面的代替，主要为了去掉 ziplist 的级联更新，同时 listpack 也是 5.0 中引入的 Stream 数据结构的底层实现之

      因为双端链表通常占用的内存比压缩列表多，所在在创建新的 redis 列表键时，
      会优先考虑使用压缩列表作为底层实现，然后在其他需要的时候，才会从压缩列表转换为双端链表实现

      服务端和客户端里使用双端链表来保存所有连接的客户端，连接的从库客户端等等等，例如 server.clients / server.slaves
*/
/* Node, List, and Iterator are the only data structures used currently. */
//链表节点
typedef struct listNode {
    struct listNode *prev; 
    struct listNode *next; 
    void *value; //具体的值
} listNode;
/*Redis 针对 list 结构实现了一个迭代器 ，用于对链表进行遍历。*/
/*
direction 决定迭代器是沿着 next 指针向后迭代，还是沿着 prev 指针向前迭代，
这个值可以是 adlist.h 中的 AL_START_HEAD 常量或 AL_START_TAIL 常量：
*/
typedef struct listIter {
    listNode *next;
    int direction; // 指定迭代的方向（从前到后还是从后到前) 
} listIter;

/* 链表 */
typedef struct list {
    listNode *head; //指向双端链表的表头节点
    listNode *tail; //指向双端链表的表尾节点 //这个特性使得 Redis 可以很方便地执行像 RPOPLPUSH 这样的命令
    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr, void *key); //这些指针指向那些用于处理不同类型值的函数。
    unsigned long len; //链表节点数量计数器
} list;

//方便操作列表
/* Functions implemented as macros */
#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

#define listSetDupMethod(l,m) ((l)->dup = (m))
#define listSetFreeMethod(l,m) ((l)->free = (m))
#define listSetMatchMethod(l,m) ((l)->match = (m))

#define listGetDupMethod(l) ((l)->dup)
#define listGetFree(l) ((l)->free)
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
list *listCreate(void); //创建
void listRelease(list *list); //释放
list *listAddNodeHead(list *list, void *value); //添加头节点
list *listAddNodeTail(list *list, void *value); //添加尾节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after); //插入节点
void listDelNode(list *list, listNode *node); //删除节点
listIter *listGetIterator(list *list, int direction); //获取迭代器
listNode *listNext(listIter *iter); //下个节点
void listReleaseIterator(listIter *iter); //释放迭代器
list *listDup(list *orig); //
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, long index); //根据索引获取节点
void listRewind(list *list, listIter *li);  //倒带
void listRewindTail(list *list, listIter *li);
void listRotate(list *list); //翻转节点

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
