/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h> //stdarg.h 是 C 标准库中用于处理‌可变参数函数‌的核心头文件，提供了一套宏机制，允许开发者定义和操作参数数量不固定的函数
#include <stdint.h> //stdint.h 是 C 语言标准库中用于定义‌固定宽度整数类型‌的头文件，旨在解决不同平台整数类型大小不一致的问题，提升代码可移植性

typedef char *sds;

/*https://blog.csdn.net/qq_34206560/article/details/90902924
  Redis3.2在2016年5月6日正式发布，相比于Redis3.0主要特征如下:
  SDS在速度和节省空间上都做了优化 
*/
//https://zhuanlan.zhihu.com/p/88635294 sds函数

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
/* 首先__attribute__ ((__packed__)) 这是告诉编译分配紧凑的内存，而不是字节对齐的方式
  内存是紧凑分配的。所以我们取到字符串的内容的时候，通过指针后退一位sds[-1] 就可以得到字符串的类型
  至少要用3位来存储类型（2的3次方=8）,1个字节8位，剩余的5位存储长度，可以满足长度小于32的短字符串

  源码中说 sdshdr5 从未使用过，但实际上是有用的，例如在 dbAdd 里对键的 sdsdup，见后面
  实际上这里说没有用，不是说 sdshdr5 类型没有用到，只是说这个结构体没有用到（除去 debug 相关
*/
struct __attribute__ ((__packed__)) sdshdr5 {
    /* msb‌ 是 ‌Most Significant Bits‌（最高有效位）的缩写 */

    /*内存优化‌：针对长度 ≤ 31 的短字符串，直接复用 flags 的高 5 位存储长度，无需额外 len 字段，节省内存*/
    /*‌高效读取‌：通过位运算快速解码长度和类型，时间复杂度为 O(1)*/
    /*msb 的适用场景‌：仅用于 sdshdr5，专门优化短字符串的内存占用*/
    /*核心优势‌：避免小字符串因单独存储 len 字段导致内存浪费，适合高频使用的短键名或值*/

    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];/*真正存储字符的内容*/
};
/*
SDS在Redis 3.2+有可能节省更多的空间，
但3.2更像一个过渡版本，Redis 4更加适合（异步删除、psync2、碎片率整理），我已经在线上大量使用，“赶紧”去用吧
*/

/*
一个 sds header 头，存储元数据，包含字符串长度len，字符串分配的容量alloc和用来表示类型的 flags
len 记录字符串长度，也是buf已用长度（不包含\0结束符），通过这个属性，我们可以直接以 O(1) 的时间复杂度获的字符串的长度
alloc：为buf分配的总长度（是不包含header和NULL结束符的），
通过 alloc - len 就可以得到字符串剩余可使用的内存空间，
当剩余空间足够所需时，追加操作就可以避免去申请内存，避免内存重分配等操作直接安全的追加
flags：占用一个字节，内存上就在buf的前一个字节的位置上。目前用了三个位来保存header的类型，还有五位是未使用的
buf 数组并没有指定数组长度，它是 C99 规范定义的柔性数组：结构体中最后一个属性可以被定义为一个大小可变的数组（该属性前必须有其它属性）。
使用 sizeof 函数计算包含柔性数组的结构体大小时，返回结构不包括柔性数组占用的内存

以 sdshdr8 为例，其中的 len 和 alloc 都是 uint8_t，是 8 位无符号整形，都只占用一个字节。当 sds 类型是 sdshdr8 时，它能表示的字符数组长度（包含数组最后的\0）不能超过 2 ** 8 = 256 位，
所以 len 和 alloc 一个字节最多都只能能表示到 0 - 255 这个范围的数字

高效操作的基础都是依赖到这个 flags 再到 header，见后面 sdslen

len：记录字符串长度，也是buf已用长度（不包含\0结束符），通过这个属性，我们可以直接以 O(1) 的时间复杂度获的字符串的长度
alloc：为buf分配的总长度（是不包含header和NULL结束符的），通过 alloc - len 就可以得到字符串剩余可使用的内存空间，
当剩余空间足够所需时，追加操作就可以避免去申请内存，避免内存重分配等操作直接安全的追加

uint8_t 适合 二进制数据存储（如图像像素、网络协议） 位掩码操作（避免符号位干扰）

之所以设计多种 header 头，是为了可以灵活的保存不同大小的字符串，为了更加节省内存空间，也为了 SDS 能够存储更大的字符串。
● 因为一些小字符串，用小类型就能表示，占用的内存空间也会更小。否则如果 header 头都是一样的话，例如用 uint64_t 来表示 len 和 alloc 的话，假设保存的字符串实际上为 5 个字节，但是加上 len 和 alloc，它两各占 8 字节，总共就要占 16 个字节，元数据比本身保存的数据都要多了，就有点浪费。
● 因为原本的 unsigned int len，能够表示的数值其实也有限，因为这个类型的数字最大也只能表示到 0~4294967295，显然如果是一个更大的字符串（这种情况下，SDS 上限为 4GB），就无法表示。issue: #757 (Remove 512 MB max value limit)
*/
struct __attribute__ ((__packed__)) sdshdr8 {

    //这个header头3个字节
    uint8_t len; /* used  已使用长度，用1字节存储 */ 
    uint8_t alloc; /* 总长度  excluding the header and null terminator  */
    unsigned char flags; /*  低3位存储类型，高5位预留 3 lsb（Least Significant Bit） of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {

    //5个字节
    uint16_t len; /* used 已使用长度，用2字节存储  */
    uint16_t alloc; /* 为buf分配的总长度（是不包含header和NULL结束符的） excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    //9个字节
    uint32_t len; /* used  已使用长度，用4字节存储  */
    uint32_t alloc; /* 为buf分配的总长度（是不包含header和NULL结束符的） excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    //17个字节
    uint64_t len; /* used 已使用长度，用8字节存储  */
    uint64_t alloc; /* 为buf分配的总长度（是不包含header和NULL结束符的） excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
//https://github.com/redis/redis/pull/2509 这个pr优化了 sds size classes - memory optimization
//而内存分配器 jemalloc/tcmalloc 等分配内存大小的单位（字节）都是 2、4、8、16、32、64 等等（这些数字叫做 bin），为了能容纳一个完整的 embstr 对象，jemalloc 最少会分配 32 字节的空间（16 < 19 < 32），这边的 19 是 redisObject 16 + sdshdr8 3 得出的。如果字符串再稍微长一点，那就是 64 字节的空间。如果总体超出了 64 字节，Redis 认为它是一个大字符串对象，不再使用 emdstr 编码存储，而会使用 raw 编码。
//而之所以选择了 64 字节，应该是因为大部分情况下 CPU Cache Line 也是 64 字节，刚好 CPU 一次访问内存就可以读到数据（能够更好利用 CPU 缓存）

//5种类型（长度1字节、2字节、4字节、8字节、小于1字节）的SDS至少要用3位来存储类型（2的3次方=8）,
//1个字节8位，剩余的5位存储长度，可以满足长度小于32的短字符串
//SDS_TYPE_MASK 7   111
#define SDS_TYPE_5  0 //长度 小于1字节 
#define SDS_TYPE_8  1    //0001
#define SDS_TYPE_16 2   //0010
#define SDS_TYPE_32 3  //0011
#define SDS_TYPE_64 4   //0100
#define SDS_TYPE_MASK 7 //0111
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

/*
整个 sds 可以根据 flags 的值来确认类型，再来直接进行指针偏移定位到 header，这里也可以看出为什么在 sds 中，内存上 header 头和 buf 字符数组是紧挨着的（并且是 packed 的）。因为字符数组才是字符串主体，紧挨着的话，可以很方便的根据 buf 向前偏移 1 位，从而获取到 flags，也就获取到类型。然后根据类型再往前偏移就可以获取到 header 头中的各个字段了，
例如其中的 len 长度字段，就可以 0(1) 的时间复杂度直接获取到 sds 字符串长度
*/
static inline size_t sdslen(const sds s) {
    //内存是连续的 所以可以使用索引-1
    unsigned char flags = s[-1];
    /*位运算判断类型*/
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: //flags 低三位为0  & 111 = 0
            return SDS_TYPE_5_LEN(flags); //右移3位
        case SDS_TYPE_8: //1个字节
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16: //2个字节
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32: //4个字节
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64: //8个字节
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            //alloc 减去 当前长度 就是剩余的空间
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen) {
    //这边获取的是字符不是指针
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/*
将SDS使用的分配器导出到使用SDS的程序中。
有时候，链接到SDS的程序可能使用一组不同的分配器，
但是可能想要分配或释放SDS将分别释放或分配的东西

为了在使用分配内存的时候 不直接调用s_malloc
*/
/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
