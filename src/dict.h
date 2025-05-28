/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* 未使用的参数会产生恼人的警告 */
/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

typedef struct dictEntry {
    /*每一个键值对都是由对象组成的*/
    /*
       数据库键总是一个字符串对象
       值 可以是字符串对象，列表对象(list)，哈希对象(hash)，集合对象(set )，有序集合对象(sorted set)
    */
    void *key;
    //// 值是一个联合体，当为后三者的时候，就不需要额外存储，利于减少内存碎片, 
    // 当然也可以是 void *，存储任何类型的数据，最早 redis1.0 版本就只是 void *
    //c 语言中 union 关键字用于声明联合体（或者叫共用体），联合体 v 的所有属性共用同一空间，同一时间只能存储其中一个属性值。
    //也就是说，下面的 v 可以存放 val、u64、s64、d 中的一个属性值。
    //用 sizeof 计算联合体的大小时，结果不会小于联合体中最大的成员属性大小
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    struct dictEntry *next; /**链表*/ //解决哈希值的冲突问题。
} dictEntry;

typedef struct dictType {
    //针对不同的类型和不同的场景，所采用的哈希算法也是不同的，下面是 redis 中基于 DJB 实现的一个字符串哈希算法，更多
    //可以参考链接：常见哈希算法和用途，比较出名的有 siphash，redis 4.0 中引进了它。3.0 之前使用的是 DJBX33A，3.0 - 4.0 使用的是 MurmurHash2。
    
    //与运算
    //上面也可以看出，索引值是通过哈希值和长度掩码进行与运算得出的：
    //● 保证了数组不会越界：ht 中数组的长度按照规定一定会是 2 的幂，所以长度的二进制形式一定会是：1000..000 类似这样，1 后面跟着一堆 0。那么长度掩码（长度-1）的二进制形式则为：0111...1111，0 后面跟着一堆1，最高位是0，跟哈希值相与的时候，结果值一定不会比数组长度大，因此也不会出现越界问题
    //● 保证元素尽可能哈希均匀分布：size 长度一定为偶数（2的幂），sizemast 长度掩码就一定是一个奇数（2的幂-1）。现在假设 size 为16，sizemask 为15，现在有两个元素需要加入，哈希值分别为 8 (二进制 1000b)，9 (二进制 1001b)，和 15 (二进制 1111b) 做与运算后，结果分别为 1000 和 1001，它们被分配到不同的索引位置上。而如果 size 长度为奇数，那么 sizemask = size - 1 就为偶数，二进制最低为为0，当做与运算的时候，碰撞到同一个索引的概率会增大。
    //● 位运算效率高：跟取余操作来讲，位运算的效率肯定是更高的。
    unsigned int (*hashFunction)(const void *key); //返回hash的函数
    void *(*keyDup)(void *privdata, const void *key);
    void *(*valDup)(void *privdata, const void *obj);
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    void (*keyDestructor)(void *privdata, void *key);
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/**
 * 其中哈希桶中的 entry 元素中保存了key 和value 指针，分别指向了实际的键和值。通过 Redis 可以在 O(1)的时间内找到键值对，只需要计算 key 的哈希值就可以定位位置，但从下图可以看出，在 4 号位置出现了冲突，两个 key 映射到了同一个位置，这就产生了哈希冲突，会导致哈希表的操作变慢。
 * 虽然 Redis 通过链式冲突解决该问题，但如果数据持续增多，产生的哈希冲突也会越来越多，会加重 Redis 的查询时间
 * 
 * 
 */
/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
typedef struct dictht {
    dictEntry **table; /** 哈希表 对应了多个哈希桶 */ // 节点指针数组
    unsigned long size; // 桶的大小（最多可容纳多少节点）
    unsigned long sizemask; // mask 码，用于地址索引计算
    unsigned long used; // 已有节点数量
} dictht;

/**
 * 为了解决上述的哈希冲突问题，
 * Redis 会对哈希表进行rehash操作，也就是增加目前的哈希桶数量，使得 key 更加分散，进而减少哈希冲突的问题
 * 
 */
typedef struct dict {
    //    // dictType 类型，指向一个结构体，结构体中包含一些常用函数指针，每种类型的字典有自己的一些函数，类似前面的双端链表的函数指针
    dictType *type; // 为哈希表中不同类型的值所使用的一族操作函数
    // 类型处理函数的私有数据指针，7.0 (#9228) 中被移除
    void *privdata;
    //// 两个哈希表，平常只有一个 ht[0] 提供服务，另一个用于 rehashing
    dictht ht[2]; // 每个字典使用两个哈希表（用于渐增式 rehash）
    //// 记录rehash的进度，-1 代表未进行
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */ // 指示 rehash 是否正在进行，如果不是则为 -1
     // 正在运行的安全迭代器数量，这个在 6.2 (#8515) 中被修改为 pauserehash
    int iterators; /* number of iterators currently running */ // 当前正在使用的 iterator 的数量
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
typedef struct dictIterator {
    dict *d;
    long index;
    int table, safe;
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { entry->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
dictEntry *dictReplaceRaw(dict *d, void *key);
int dictDelete(dict *d, const void *key);
int dictDeleteNoFree(dict *d, const void *key);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(unsigned int initval);
unsigned int dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
