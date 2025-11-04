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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "sdsalloc.h"

//原生 C 字符串的问题：
//1. C 字符串不允许\0出现在字符串的中间，因为放在中间会被认为是结尾符，不是二进制安全的（二进制安全：只会严格的按照二进制数据存取，不会试图去解析它们）
//2. C 字符串并不能高效的计算字符串长度（完整遍历一遍字符串）和追加字符串（内存分配加拷贝，还要考虑操作是否安全）这两类操作，时间复杂度均是 O(N)
//https://zhuanlan.zhihu.com/p/396423028
//
//将 static 和 inline 组合在一起使用时，函数将具有以下特性：
//‌内联优化‌：编译器会尝试将函数体直接插入到调用点，以减少函数调用的开销。
//限制作用域‌：函数只能在定义它的文件内部被访问，这有助于减少符号表的大小和避免命名冲突。
//编译单元内优化‌：由于函数是 static 的，编译器可以在单个编译单元内对其进行更深入的优化，因为它知道该函数不会在其他编译单元中被调用。

/*
    type和SDS_TYPE_MASK掩码比较
    返回对应类型的结构体所需要的字节大小
*/
static inline int sdsHdrSize(char type) {

    //位操作的经典比较
    //SDS_TYPE_MASK 0111
    switch(type&SDS_TYPE_MASK) {
        case SDS_TYPE_5: //0000
            return sizeof(struct sdshdr5); 
        case SDS_TYPE_8: //0001
            return sizeof(struct sdshdr8);
        case SDS_TYPE_16: //0010
            return sizeof(struct sdshdr16);
        case SDS_TYPE_32: //0011
            return sizeof(struct sdshdr32);
        case SDS_TYPE_64: //0100
            return sizeof(struct sdshdr64);
    }
    return 0;
}

/* 
    判断参数string_size的大小
    来返回对应的类型
    如果小于1<<5=32 ，就是SDS_TYPE_5
    如果小于1<<8=256，就是SDS_TYPE_8
    如果小于1<<16=65536，就是SDS_TYPE_16
    如果小于1ll<<32=4,294,967,296，就是SDS_TYPE_32
    否则就是SDS_TYPE_64
*/
static inline char sdsReqType(size_t string_size) {
    //100000 = 2的5次方= 32
    //长度小于32 用SDS_TYPE_5 最大为011111
    //sdshdr5 类型最大可以存储长度为 2 ** 5 - 1 的字符串

    // 1<<5 100000 = 2的5次方 - 1
    if (string_size < 1<<5)
        return SDS_TYPE_5;
    if (string_size < 1<<8) //不能超过 2 ** 8 = 256 位
        return SDS_TYPE_8;
    if (string_size < 1<<16)
        return SDS_TYPE_16;
    if (string_size < 1ll<<32)
        return SDS_TYPE_32;
    return SDS_TYPE_64;
}

 // 创建一个 通过init指针和初始化长度指定内容的 sds字符串
 //如果init为空 那么就用0字节 初始化
 //字符串总是以null结束，即使用sdsnewlen("abc",3)创建
/* Create a new sds string with the content specified by the 'init' pointer
 * and 'initlen'.
 * If NULL is used for 'init' the string is initialized with zero bytes.
 *
 * The string is always null-termined (all the sds strings are, always) so
 * even if you create an sds string with:
 *
 * mystring = sdsnewlen("abc",3);
 *
 * You can print the string with printf() as there is an implicit \0 at the
 * end of the string. However the string is binary safe and can contain
 * \0 characters in the middle, as the length is stored in the sds header. */
sds sdsnewlen(const void *init, size_t initlen) {
    void *sh;
    sds s;

    /*
        1、根据初始长度判断需要的字符串类型
        2. 如果类型为SDS_TYPE_5且长度为0，那么类型会变为SDS_TYPE_8,
           因为长度为0的字符串通常是为了追加而创建的，而SDS_TYPE_5在这方面不合适
           所以在dbAdd的时候 键可以成为SDS_TYPE_5 ,因为键通常不变。
        
        3、根据类型获取对应sds结构体所需要的内存大小。
        4. 分配内存：结构体大小+初始化字符串大小+null字符
        5、如果没有初始化字符串，那么就把所有字符设为0
        6、如果这时候sh指针为null,那么就返回null
        7. s指针指向柔性数组 也就是真正存储字符串的地方
           fp通过s指针后退一位指向flag

        8. 根据类型选择  
           如果是SDS_TYPE_5，那么就设置flag，
           flag设置为初始长度向左偏移3位后再和类型做或运算的结果
           如果是其他类型，那么就把s指针后退到结构体开始为止
           通过宏来适配不同类型的结构体
           fp通过s指针后退一位指向flag

        9.如果长度和初始字符串都有了，那么就通过memcpy复制初始字符串到s中
          s指向柔性数组

    */

    //判断需要的类型
    //initlen小于32 代表小于32个字节
    //小于32个字节的话5位才能放下
    char type = sdsReqType(initlen);
    //type为SDS_TYPE_5的空字符串直接转换为SDS_TYPE_8

    //因为空字符串经常是为了追加而创建
    /* Empty strings are usually created in order to append. Use type 8
     * since type 5 is not good at this. */
    if (type == SDS_TYPE_5 && initlen == 0) type = SDS_TYPE_8;
    //根据type获取到相应的结构体长度
    int hdrlen = sdsHdrSize(type);
    unsigned char *fp; /* flags pointer. */

    //malloc的内存 没有在这个函数最后去释放
    //跟C专家编程里P49页写的 显示分配内存，保存返回的值。
    sh = s_malloc(hdrlen+initlen+1); //+1代表最后的\0
    if (!init) //可以这么判断
        memset(sh, 0, hdrlen+initlen+1); //3.0使用 sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    
    //为啥不放在分配内存之后 就判断？
    //高版本把这个判断放到了 malloc之后了
    if (sh == NULL) return NULL;
    //s指向柔性数组 指针偏移
    s = (char*)sh+hdrlen;
    //指向flag
    fp = ((unsigned char*)s)-1;
    switch(type) {
        case SDS_TYPE_5: {
            //小于32个字节的话5位才能放下
            //flags占1个字节，其低3位（bit）表示type，高5位（bit）表示长度，能表示的长度区间为0～31（2的5次方-1)
            //flags: 00000 000
            //SDS_TYPE_BITS 为 3

            //比如最大长度为00 011111 左移3位 就变为11111  000
            *fp = type | (initlen << SDS_TYPE_BITS);
            break;
        }
        case SDS_TYPE_8: {
            //利用宏定义一个结构体指针
            //指针指向sdshdr8起始地址
            SDS_HDR_VAR(8,s); //为了使得sh是SDS_TYPE_8类型的指针
            sh->len = initlen; //不包含\0
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            //sh 是SDS_HDR_VAR里设置的
            sh->len = initlen; //不包含\0 
            sh->alloc = initlen; // 高版本做了修改
            *fp = type;
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            sh->len = initlen; //不包含\0
            sh->alloc = initlen; // 高版本做了修改
            *fp = type;
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            sh->len = initlen; //不包含\0
            sh->alloc = initlen; // 高版本做了修改
            *fp = type;
            break;
        }
    }
    //memcpy 是 C/C++ 标准库中用于‌复制内存块内容‌的函数。
    //它的作用是将一块内存中的数据按字节逐字节地复制到另一块内存中，不关心数据的具体类型或内容（比如字符串、结构体、数组等）。
    //返回值‌：返回目标内存的地址 dest。
    //不处理内存重叠‌：
    //如果源内存（src）和目标内存（dest）有重叠（比如部分区域相同），使用 memcpy 会导致未定义行为（可能覆盖数据）。此时应改用 memmove（它会处理内存重叠问题）。
    if (initlen && init)
        memcpy(s, init, initlen); //使用memcpy复制
    s[initlen] = '\0';
    return s;
}

/* Create an empty (zero length) sds string. Even in this case the string
 * always has an implicit null term. */
/*
即使在这种情况下，字符串也总是有一个隐式的空项
*/
sds sdsempty(void) {
    return sdsnewlen("",0);
}

/*  
    从一个以空结尾的C字符串开始创建一个新的sds字符串
*/
/* Create a new sds string starting from a null terminated C string. */
sds sdsnew(const char *init) {
    /*使用strlen计算字符串长度*/
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

/*
 复制一个sds字符串
*/
/* Duplicate an sds string. */
sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

/* Free an sds string. No operation is performed if 's' is NULL. */
void sdsfree(sds s) {
    /*如果本身为空指针就直接返回*/
    if (s == NULL) return;
    //(char*)s-sdsHdrSize(s[-1]) 指向分配空间的开始地址

    /*
        1.先后退一位获得字符代表的类型
        2.通过sdsHdrSize获取类型对应的结构体字节大小
        3. 再通过s指针后提结构体大小的字节数 到达起始位置
    */
    s_free((char*)s-sdsHdrSize(s[-1]));
}

/*
 将sds字符串长度设置为使用strlen()获得的长度，
 因此只考虑到第一个空项字符的内容

 当以某种方式手动入侵sds字符串时，此函数很有用
 就像下面的例子一样:
*/
/* Set the sds string length to the length as obtained with strlen(), so
 * considering as content only up to the first null term character.
 *
 * This function is useful when the sds string is hacked manually in some
 * way, like in the following example:
 *
 * s = sdsnew("foobar");
 * s[2] = '\0';
 * sdsupdatelen(s);
 * printf("%d\n", sdslen(s));
 *
 * The output will be "2", but if we comment out the call to sdsupdatelen()
 * the output will be "6" as the string was modified but the logical length
 * remains 6 bytes. */
void sdsupdatelen(sds s) {
    /*通过strlen获取真正的大小*/
    int reallen = strlen(s);
    sdssetlen(s, reallen);
}

/*
    在不释放 SDS 的字符串空间的情况下，
    重置 SDS 所保存的字符串为空字符串。

    所有现有的缓冲区不会被丢弃，而是被设置为空闲空间，
    以便下一个追加操作将不需要分配之前可用的字节数
*/
/* Modify an sds string in-place to make it empty (zero length).
 * However all the existing buffer is not discarded but set as free space
 * so that next append operations will not require allocations up to the
 * number of bytes previously available. */
void sdsclear(sds s) {
    sdssetlen(s, 0);
    s[0] = '\0';
}

/*
  readQueryFromClient 从客户端读取的时候 会提前调用这个函数扩展空间
*/
/*
扩大sds字符串末尾的可用空间，以便调用方确保在调用此函数后，可以覆盖字符串末尾后最多 addlen 字节的内容，再加上null项的一个字节
注意：这不会改变sdslen（）返回的sds字符串的*length*，而只会改变我们的可用缓冲区空间
*/
/* Enlarge the free space at the end of the sds string so that the caller
 * is sure that after calling this function can overwrite up to addlen
 * bytes after the end of the string, plus one more byte for nul term.
 *
 * Note: this does not change the *length* of the sds string as returned
 * by sdslen(), but only the free buffer space we have. */
sds sdsMakeRoomFor(sds s, size_t addlen) {
    void *sh, *newsh;
    //获取剩余空间
    size_t avail = sdsavail(s);
    size_t len, newlen;
    //类型
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen;

    /* 如果 有足够的空间剩余 就直接返回*/
    /* Return ASAP if there is enough space left. */
    if (avail >= addlen) return s;
    //获取sds原来的len 就是 结构体的len字段
    len = sdslen(s);
    //指向结构体开始位置
    sh = (char*)s-sdsHdrSize(oldtype);

    //新的长度= 原来的长度+添加的长度
    newlen = (len+addlen);
    //当字符串长度小于 1M 时， 扩容都是加倍现有的空间，
    //如果超过 1M，扩容时一次只会多扩 1M 的空间。需要注意的是 字符串最大长度为 512M。
    if (newlen < SDS_MAX_PREALLOC)
        newlen *= 2;
    else
        newlen += SDS_MAX_PREALLOC;

    /*判断新的长度需要的类型*/
    type = sdsReqType(newlen);

    /*
        不适用SDS_TYPE_5 类型
        用户正在追加字符串，而类型5无法记住空白，
        因此在每次追加操作时必须调用sdsMakeRoomFor()。
    */
    /* Don't use type 5: the user is appending to the string and type 5 is
     * not able to remember empty space, so sdsMakeRoomFor() must be called
     * at every appending operation. */
    if (type == SDS_TYPE_5) type = SDS_TYPE_8;

    /*计算 新的类型对应的结构体内存空间大小 */
    hdrlen = sdsHdrSize(type);

    if (oldtype==type) {
        //// 如果新老类型一致, 对sh的空间重新分配下, 不会改变原来里面的数据
        // realloc相关文档: https://man.cx/realloc
        
        //redis深度历险：
        //realloc 可能会重新分配新的内存空间，并将之前的内容一次性拷贝到新的地址，
        //也可 能在原有的地址上进行扩展，这时就不需要进行旧内容的内存拷贝
        newsh = s_realloc(sh, hdrlen+newlen+1);
        if (newsh == NULL) return NULL;
        // 传进来的sds s指针, 变成新header + 头长度, 即新的sds s指针
        s = (char*)newsh+hdrlen;
    } else {
        //因为头部大小变了，所以需要向前移动字符串，不能使用realloc
        /* Since the header size changes, need to move the string forward,
         * and can't use realloc */

        /*使用malloc重新分配空间*/
        newsh = s_malloc(hdrlen+newlen+1);
        if (newsh == NULL) return NULL;

        /*复制原来的字符串 到新的buf*/
        memcpy((char*)newsh+hdrlen, s, len+1);
        //释放原来的空间
        s_free(sh);
        //s指向新的buf数组
        s = (char*)newsh+hdrlen;
        //倒退1个字节
        s[-1] = type;
        //设置新的sds长度 ，长度还是没变
        //因为s是指向新的结构体了
        sdssetlen(s, len);
    }
    //设置给字符串分配的总长度，不包括结构体本身的长度
    //也就是alloc 字段 变为新的长度
    sdssetalloc(s, newlen);
    return s;
}

/*
 重新分配sds字符串，使其末尾没有空闲空间。所包含的字符串保持不变，但下一个连接操作将需要重新分配
 调用之后，传递的sds字符串不再有效，所有引用必须用调用返回的新指针替换
*/
//● 如果 sds 释放前后类型没变，或者本身类型就相对较大（ type > SDS_TYPE_8），直接进行 realloc 重新分配内存空间
//● 如果 sds 释放前后类型变化，或者本身类型很小，是进行 malloc + memcpy + free，分配新空间，内存拷贝，释放老空间
/* Reallocate the sds string so that it has no free space at the end. The
 * contained string remains not altered, but next concatenation operations
 * will require a reallocation.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdsRemoveFreeSpace(sds s) {
    void *sh, *newsh;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen;
    /*sds的实际长度*/
    size_t len = sdslen(s);
    sh = (char*)s-sdsHdrSize(oldtype);

    /*因为老的类型有可能是大的 实际长度所需要的类型可能是小的*/
    type = sdsReqType(len);
    //结构体本身的长度
    hdrlen = sdsHdrSize(type);
    if (oldtype==type) {
        // 使用realloc 重新分配hdrlen+len+1长度的空间
        newsh = s_realloc(sh, hdrlen+len+1);
        if (newsh == NULL) return NULL;
        s = (char*)newsh+hdrlen;
    } else {
        //如果类型改变
        //分配新空间，
        newsh = s_malloc(hdrlen+len+1);
        if (newsh == NULL) return NULL;
        //内存拷贝
        memcpy((char*)newsh+hdrlen, s, len+1);
        //释放老空间
        s_free(sh);
        //指向新的buf柔性数组
        s = (char*)newsh+hdrlen;
        //倒退一位设置类型
        s[-1] = type;
        //设置长度
        //内部需要返回到结构体初始位置
        sdssetlen(s, len);
    }
    //内部又要需要返回到结构体初始位置
    //设置alloc 为len 所以没有剩余空间
    sdssetalloc(s, len);
    return s;
}

/**
 * 返回整个sds已分配的空间大小
 * 包含
 * 1.sds指针前的头部
 * 2.实际字符串的空间
 * 3.空闲缓存大小
 * 4.隐式的null字符
 * 
 */
/* Return the total size of the allocation of the specifed sds string,
 * including:
 * 1) The sds header before the pointer.
 * 2) The string.
 * 3) The free buffer at the end if any.
 * 4) The implicit null term.
 */
size_t sdsAllocSize(sds s) {
    size_t alloc = sdsalloc(s);
    return sdsHdrSize(s[-1])+alloc+1;
}

/*
 返回实际分配空间的起始地址指针
 通常SDS字符串由字符串缓冲区的开头引用
*/
/* Return the pointer of the actual SDS allocation (normally SDS strings
 * are referenced by the start of the string buffer). */
void *sdsAllocPtr(sds s) {
    return (void*) (s-sdsHdrSize(s[-1]));
}

/*
   这个函数是在调用 sdsMakeRoomFor() 对字符串进行扩展，
 * 然后用户在字符串尾部写入了某些内容之后，
 * 用来正确更新 free 和 len 属性的。
 * 
 * 此函数用于在用户调用sdsMakeRoomFor（）后修复字符串长度，
 * 在当前字符串结束后写入内容，最后需要设置新的长度
 * 
    注意：可以使用负增量来右切字符串
 * 
 * 增加sds的长度len字段
 * 并根据‘incr’减少字符串末尾的左侧自由空间。还要在字符串的新末尾设置空项
 * 
 * 使用示例：
 * 使用sdsIncrLen（）和sdsMakeRoomFor（）可以挂载以下模式，将来自内核的字节转移到sds字符串的末尾，而无需复制到中间缓冲区
*/
/* Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string.
 *
 * This function is used in order to fix the string length after the
 * user calls sdsMakeRoomFor(), writes something after the end of
 * the current string, and finally needs to set the new length.
 *
 * Note: it is possible to use a negative increment in order to
 * right-trim the string.
 *
 * Usage example:
 *
 * Using sdsIncrLen() and sdsMakeRoomFor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 *
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sdsIncrLen(s, nread);
 */
void sdsIncrLen(sds s, int incr) {
    unsigned char flags = s[-1];
    size_t len;
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            //flag pointer
            unsigned char *fp = ((unsigned char*)s)-1;
            //获取旧长度
            unsigned char oldlen = SDS_TYPE_5_LEN(flags);
            assert((incr > 0 && oldlen+incr < 32) || (incr < 0 && oldlen >= (unsigned int)(-incr)));
            *fp = SDS_TYPE_5 | ((oldlen+incr) << SDS_TYPE_BITS);
            len = oldlen+incr;
            break;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            assert((incr >= 0 && sh->alloc-sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
             break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            assert((incr >= 0 && sh->alloc-sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (unsigned int)incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (uint64_t)incr) || (incr < 0 && sh->len >= (uint64_t)(-incr)));
            len = (sh->len += incr);
            break;
        }
        default: len = 0; /* Just to avoid compilation warnings. */
    }
    //新的长度末尾加上null
    s[len] = '\0';
}

/*
 
    增加sds以具有指定的长度。不属于sds原始长度的字节将被设置为零
    如果设置的长度小于当前长度，则不执行任何操作
*/
/* Grow the sds to have the specified length. Bytes that were not part of
 * the original length of the sds will be set to zero.
 *
 * if the specified length is smaller than the current length, no operation
 * is performed. */
sds sdsgrowzero(sds s, size_t len) {
    //当前长度
    size_t curlen = sdslen(s);
    //如果设置的长度小于当前长度 直接返回
    if (len <= curlen) return s;
    //用设置长度减去现在的长度 扩展空间
    s = sdsMakeRoomFor(s,len-curlen);
    if (s == NULL) return NULL;

    /*确保添加的区域不包含垃圾*/
    /* Make sure added region doesn't contain garbage */
    memset(s+curlen,0,(len-curlen+1)); /* also set trailing \0 byte */
    /*设置长度到新的结构体*/
    sdssetlen(s, len);
    return s;
}

/*
 将指定的由‘len’字节的‘t’指向的二进制安全字符串附加到指定的sds字符串‘s’的末尾。
 调用之后，传递的sds字符串不再有效，所有引用必须用调用返回的新指针替换
*/
/* Append the specified binary-safe string pointed by 't' of 'len' bytes to the
 * end of the specified sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscatlen(sds s, const void *t, size_t len) {
    //O(1)获取当前sds的长度, 从sdshdr->len字段里获取
    size_t curlen = sdslen(s);
    //根据要追加的长度，判断 / 确保有足够的空余内存空间
    //内部有释放原来结构体和字符数组占用的空间
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;
    //拷贝 t 到s中， 从 s + curlen 位置开始, 往后 len 个长度,
    memcpy(s+curlen, t, len);
    // 重新设置sds的长度
    sdssetlen(s, curlen+len);
    // 末尾用\0结束
    s[curlen+len] = '\0';
    return s;
}
/*将给定字符串 t 追加到 sds 的末尾*/
/* Append the specified null termianted C string to the sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

/* Append the specified sds 't' to the existing sds 's'.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
/*const 形参*/
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}

/*
破坏性地修改sds字符串‘s’
以保存由‘t’指向的长度为‘len’字节的指定二进制安全字符串
*/
/* Destructively modify the sds string 's' to hold the specified binary
 * safe string pointed by 't' of length 'len' bytes. */
sds sdscpylen(sds s, const char *t, size_t len) {
    
    /*
        如果现在sds的分配长度比 t的长度小
        那么就扩展空间
    */
    if (sdsalloc(s) < len) {
        s = sdsMakeRoomFor(s,len-sdslen(s));
        if (s == NULL) return NULL;
    }
    //拷贝t到s指向的字符数组中
    //原来的字符串没了
    memcpy(s, t, len);
    s[len] = '\0';
    sdssetlen(s, len);
    return s;
}

/* Like sdscpylen() but 't' must be a null-termined string so that the length
 * of the string is obtained with strlen(). */
sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

/*
  帮助sdscatlonglong（）执行实际的数字->字符串转换。
  ‘s’必须指向一个至少有SDS_LLSTR_SIZE字节空间的字符串

    函数返回存储在s以空结束的字符串的长度 
*/
/* Helper for sdscatlonglong() doing the actual number -> string
 * conversion. 's' must point to a string with room for at least
 * SDS_LLSTR_SIZE bytes.
 *
 * The function returns the length of the null-terminated string
 * representation stored at 's'. */
#define SDS_LLSTR_SIZE 21
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    /*
    生成字符串表示，此方法生成一个反向字符串。
    */
    /* Generate the string representation, this method produces
     * an reversed string. */
    v = (value < 0) ? -value : value;
    p = s;
    do {
        //'0' 是字符 '0' 的ASCII码值（十进制48），v % 10 获取整数 v 的个位数（0~9），两者相加后得到该数字对应的ASCII字符
        *p++ = '0'+(v%10);
        v /= 10; //整除
    } while(v);

    //加上- 符号
    if (value < 0) *p++ = '-';

    /*计算长度 添加null*/
    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /*倒转字符串*/
    /* Reverse the string. */
    p--; //回到字符末尾
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/*
相同的sdsll2str()，但用于unsigned long long类型
就是没有负号的
*/
/* Identical sdsll2str(), but for unsigned long long type. */
int sdsull2str(char *s, unsigned long long v) {
    char *p, aux;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/*
 从一个long long值创建一个sds字符串。它比……快得多
*/
/* Create an sds string from a long long value. It is much faster than:
 *
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(buf,value);

    return sdsnewlen(buf,len);
}

/*
 与sdscatprintf（）类似，但获得va_list而不是variadic
*/
/* Like sdscatprintf() but gets va_list instead of being variadic. */
sds sdscatvprintf(sds s, const char *fmt, va_list ap) {
    //va_list 是 C 语言中处理可变参数函数（Variadic Function）的核心机制，用于访问不确定数量和类型的参数
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    /*
      为了速度我们尝试用静态buffer
      如果不可能我们转换为堆分配
    */
    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    if (buflen > sizeof(staticbuf)) {
        //转化为堆分配
        //buf指向堆内存
        buf = s_malloc(buflen);
        
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    /*
        每次我们无法将字符串放入当前缓冲区大小时，尝试将缓冲区大小增大两倍
    */
    /* Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size. */
    while(1) {
        //设置哨兵
        //vsnprintf 保证在缓冲区末尾（buf[buflen-1]）写入 \0，若检测 buflen-1 会误判所有情况为需要扩容
        //vsnprintf 默认会在输出字符串的 ‌末尾添加 \0‌，前提是缓冲区大小 n 允许（即 n ≥ 1）15。
        //若缓冲区足够大（即格式化后的字符串长度 + 终止符 ≤ n），则完整写入字符串并添加 \0。
        //若缓冲区不足（即格式化后的字符串长度 ≥ n），则截断输出内容，并在缓冲区最后一个合法位置（buf[n-1]）写入 \0
        buf[buflen-2] = '\0';
        //va_copy 是 C 语言中用于复制可变参数列表的宏，属于 <stdarg.h> 标准库的一部分15。其核心功能与用法如下
        va_copy(cpy,ap);

        /*
        vsnprintf 是 C 语言中用于安全格式化字符串的函数，属于可变参数处理函数族，
        核心功能是将格式化数据从可变参数列表写入指定大小的缓冲区，避免缓冲区溢出风险

        */
        //也就是把fmt 格式化字符串 加上参数后写入buf中
        vsnprintf(buf, buflen, fmt, cpy);
        va_end(cpy);
        //哨兵被覆盖
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) s_free(buf);
            buflen *= 2; //长度*2
            buf = s_malloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    //最后，将获得的字符串连接到SDS字符串并返回它
    /* Finally concat the obtained string to the SDS string and return it. */
    t = sdscat(s, buf);
    //如果不是静态缓冲区 那就需要释放
    if (buf != staticbuf) s_free(buf);
    return t;
}

/*
 向sds字符串‘s’追加使用类似打印格式说明符获得的字符串

 调用之后，修改后的sds字符串不再有效，所有引用必须用调用返回的新指针替换
*/
/* Append to the sds string 's' a string obtained using printf-alike format
 * specifier.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsnew("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * 通常需要从头创建具有类似print格式的字符串。如果需要，只需使用sdsempty（）作为目标字符串
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sdsempty() as the target string:
 *
 * s = sdscatprintf(sdsempty(), "... your format ...", args);
 */
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    //va_start 用于初始化 va_list 类型的变量。
    //该变量后续可通过 va_arg 和 va_end 访问和管理可变参数。
    //必须在函数内其他参数处理前调用，确保参数指针的正确性

    //参数要求‌
    //第一个参数：需初始化的 va_list 变量。
    //第二个参数：函数中最后一个固定参数（即可变参数前的参数）。
    //通过固定参数的地址推算可变参数在栈中的起始位置
    va_start(ap, fmt);
    t = sdscatvprintf(s,fmt,ap);
    //用于释放 va_list 类型变量占用的资源，结束对可变参数的访问
    //va_end 通过清理参数列表和重置指针，确保可变参数函数的安全退出
    va_end(ap);
    return t;
}

/* This function is similar to sdscatprintf, but much faster as it does
 * not rely on sprintf() family functions implemented by the libc that
 * are often very slow. Moreover directly handling the sds string as
 * new data is concatenated provides a performance improvement.
 *
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */
sds sdscatfmt(sds s, char const *fmt, ...) {
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap,fmt);
    f = fmt;    /* 下一个要处理的格式说明符字节 Next format specifier byte to process. */
    i = initlen; /* 写入dest str的下一个字节的位置 Position of the next byte to write to dest str. */
    while(*f) {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        /* Make sure there is always space for at least 1 char. */
        /*判断是否还有空间*/
        /*用alloc -len 判断*/
        if (sdsavail(s)==0) {
            //没有就扩充
            s = sdsMakeRoomFor(s,1);
        }

        switch(*f) {
        case '%':
            next = *(f+1);
            f++;
            switch(next) {
            case 's':
            case 'S':
                //va_arg 是 C 语言中处理可变参数的核心宏，其作用为 ‌从可变参数列表中按类型逐个提取参数
                str = va_arg(ap,char*);
                //判断是提取 c字符串还是sds的长度
                l = (next == 's') ? strlen(str) : sdslen(str);
                //如果s的长度小于参数的长度，就扩充空间
                if (sdsavail(s) < l) {
                    s = sdsMakeRoomFor(s,l);
                }
                //拷贝str到s+i的位置
                memcpy(s+i,str,l);
                //sds的Len字段长度增加
                sdsinclen(s,l);
                i += l; //offset偏移
                break;
            case 'i':
            case 'I':
                if (next == 'i')
                    num = va_arg(ap,int);
                else
                    num = va_arg(ap,long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    //把num转换成字符串，返回长度
                    l = sdsll2str(buf,num);
                    if (sdsavail(s) < l) {
                        s = sdsMakeRoomFor(s,l);
                    }
                    memcpy(s+i,buf,l);
                    sdsinclen(s,l);
                    i += l;
                }
                break;
            case 'u':
            case 'U':
                if (next == 'u')
                    unum = va_arg(ap,unsigned int);
                else
                    unum = va_arg(ap,unsigned long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsull2str(buf,unum);
                    if (sdsavail(s) < l) {
                        s = sdsMakeRoomFor(s,l);
                    }
                    memcpy(s+i,buf,l);
                    sdsinclen(s,l);
                    i += l;
                }
                break;
            default: /* Handle %% and generally %<unknown>. */
                s[i++] = next; //直接把参数字符存进去
                sdsinclen(s,1);
                break;
            }
            break;
        default:
            s[i++] = *f;
            sdsinclen(s,1);
            break;
        }
        f++;
    }
    va_end(ap);

    /* Add null-term */
    s[i] = '\0';
    return s;
}

/* Remove the part of the string from left and from right composed just of
 * contiguous characters found in 'cset', that is a null terminted C string.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsnew("AA...AA.a.aa.aHelloWorld     :::");
 * s = sdstrim(s,"Aa. :");
 * printf("%s\n", s);
 *
 * Output will be just "Hello World".
 */
sds sdstrim(sds s, const char *cset) {
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;
    ep = end = s+sdslen(s)-1; //不包含字符串末尾的null字符
    //strchr 是 C 语言标准库中的字符串查找函数，用于定位字符在字符串中的首次出现位置
    //str‌：待搜索的字符串（需以 \0 结尾）34。
    //‌c‌：目标字符（以 int 类型传递，实际按 ASCII 值处理)
    while(sp <= end && strchr(cset, *sp)) sp++;
    while(ep > sp && strchr(cset, *ep)) ep--;
    len = (sp > ep) ? 0 : ((ep-sp)+1); //len为还剩余的长度
    // 只是维护 len 字段，并没有进行后面内存的回收
    //由src所指内存区域复制count个字节到dest所指内存区域。
    if (s != sp) memmove(s, sp, len);
    //只是把len处设为\0
    s[len] = '\0';
    sdssetlen(s,len);
    return s;
}

/*
  将字符串转换成一个更小的（或相等的）字符串，其中只包含由‘start’和‘end’索引指定的子字符串
  开始和结束可以是负数，其中-1表示字符串的最后一个字符，-2表示倒数第二个字符，依此类推

  间隔是包含的，因此开始和结束字符将是结果字符串的一部分

  字符串被就地修改
*/
/* Turn the string into a smaller (or equal) string containing only the
 * substring specified by the 'start' and 'end' indexes.
 *
 * start and end can be negative, where -1 means the last character of the
 * string, -2 the penultimate character, and so forth.
 *
 * The interval is inclusive, so the start and end characters will be part
 * of the resulting string.
 *
 * The string is modified in-place.
 *
 * Example:
 *
 * s = sdsnew("Hello World");
 * sdsrange(s,1,-1); => "ello World"
 */
void sdsrange(sds s, int start, int end) {
    size_t newlen, len = sdslen(s);
    //本身长度为0直接返回
    if (len == 0) return;

    //如果start小于0 那start就是从尾部开始
    //比如-1 那就是len-1开始
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    //如果start小于0 那start就是从尾部开始
    //比如-1 那就是len-1开始
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }
    //如果start大于end 新长度就是0  
    //否则就是end-start+1
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }
    //难道start必须大于0吗 
    //新版本改正了
    if (start && newlen) memmove(s, s+start, newlen);
    s[newlen] = 0;
    sdssetlen(s,newlen);
}

/*
对sds字符串‘s’中的每个字符应用tolower（）
大写转换成小写
*/
/* Apply tolower() to every character of the sds string 's'. */
void sdstolower(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

/*
/*
对sds字符串‘s’中的每个字符应用tolower（）
小写转换成大写
*/
/* Apply toupper() to every character of the sds string 's'. */
void sdstoupper(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}

/*
使用memcmp 比较两个sds字符串
正数 代表s1>s2
负数 代表s1<s2
0代表 二进制字符串正好相同
*/
/* Compare two sds strings s1 and s2 with memcmp().
 *
 * Return value:
 *
 *     positive if s1 > s2.
 *     negative if s1 < s2.
 *     0 if s1 and s2 are exactly the same binary string.
 *
 * 较长的字符串被认为比较小的字符串大
 * If two strings share exactly the same prefix, but one of the two has
 * additional characters, the longer string is considered to be greater than
 * the smaller one. */
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1,s2,minlen);
    //返回长度的差值
    if (cmp == 0) return l1-l2;
    return cmp;
}

/*
用分隔符分隔‘sep’中的‘s’。
返回一个sds字符串数组。
Count将引用返回的令牌数来设置
*/
/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if (seplen < 1 || len < 0) return NULL;

    tokens = s_malloc(sizeof(sds)*slots);
    if (tokens == NULL) return NULL;

    if (len == 0) {
        *count = 0;
        return tokens;
    }
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {
            sds *newtokens;

            slots *= 2; //槽扩充
            newtokens = s_realloc(tokens,sizeof(sds)*slots);
            if (newtokens == NULL) goto cleanup;
            tokens = newtokens;
        }
        /* search the separator */
        /*查找分隔符*/
        /*如果分隔符只有1个 且 当前字符正好等于分隔符*/
        /*或者所有分隔符和 当前位置开始的字符正好相同  */
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {
            //从s+start处拷贝j-start长度的字符串 ，因为是拷贝的所以s还可以使用
            tokens[elements] = sdsnewlen(s+start,j-start);
            if (tokens[elements] == NULL) goto cleanup;
            elements++;//元素数量增加一个
            start = j+seplen; //start开始偏移到分隔符对应的字符串后面
            j = j+seplen-1; /* skip the separator */ /*分隔符有多个的情况就适用了*/
            //比如 abcd 分隔符是bc的时候 那么 j=1的时候 seplen=2 这个时候 j要等于2 才会跳过分隔符
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start,len-start);
    if (tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        //内部就判断tokens指针是否为空
        s_free(tokens);
        *count = 0;
        return NULL;
    }
}

/* Free the result returned by sdssplitlen(), or do nothing if 'tokens' is NULL. */
void sdsfreesplitres(sds *tokens, int count) {
    if (!tokens) return;
    while(count--)
        sdsfree(tokens[count]);
    s_free(tokens);
}

/*
在sds字符串“s”后面附加一个转义字符串表示形式，
其中所有不可打印的字符（使用isprint（）测试）都转换为转义，格式为“\n\r\a....”或“\x<hex-number>”

    x = sdsnewlen("\a\n\0foo\r",7);
        y = sdscatrepr(sdsempty(),x,sdslen(x));
        test_cond("sdscatrepr(...data...)",
            memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)
*/
/* Append to the sds string "s" an escaped string representation where
 * all the non-printable characters (tested with isprint()) are turned into
 * escapes in the form "\n\r\a...." or "\x<hex-number>".
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscatrepr(sds s, const char *p, size_t len) {
    s = sdscatlen(s,"\"",1);
    while(len--) {
        switch(*p) {
        case '\\':
        case '"':
            s = sdscatprintf(s,"\\%c",*p);
            break;
        case '\n': s = sdscatlen(s,"\\n",2); break;
        case '\r': s = sdscatlen(s,"\\r",2); break;
        case '\t': s = sdscatlen(s,"\\t",2); break;
        case '\a': s = sdscatlen(s,"\\a",2); break;
        case '\b': s = sdscatlen(s,"\\b",2); break;
        default:
            if (isprint(*p))
                s = sdscatprintf(s,"%c",*p);
            else
                s = sdscatprintf(s,"\\x%02x",(unsigned char)*p);
            break;
        }
        p++;
    }
    return sdscatlen(s,"\"",1);
}

/* Helper function for sdssplitargs() that returns non zero if 'c'
 * is a valid hex digit. */
int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* Helper function for sdssplitargs() that converts a hex digit into an
 * integer from 0 to 15 */
int hex_digit_to_int(char c) {
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

/*
函数在成功时返回分配的令牌，即使输入字符串为空，
或者如果输入包含不平衡的引号或后接非空格字符的闭引号，则返回NULL，如：“foo”bar或“foo”

用于解析命令行参数的工具函数，其核心作用是将包含空格、引号的字符串按规则分割为参数数组
*/
/* Split a line into arguments, where every argument can be in the
 * following programming-language REPL-alike form:
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * The number of arguments is stored into *argc, and an array
 * of sds is returned.
 *
 * The caller should free the resulting array of sds strings with
 * sdsfreesplitres().
 *
 * Note that sdscatrepr() is able to convert back a string into
 * a quoted string in the same format sdssplitargs() is able to parse.
 *
 * The function returns the allocated tokens on success, even when the
 * input string is empty, or NULL if the input contains unbalanced
 * quotes or closed quotes followed by non space characters
 * as in: "foo"bar or "foo'
 */
sds *sdssplitargs(const char *line, int *argc) {
    const char *p = line;
    char *current = NULL;
    char **vector = NULL;

    *argc = 0;
    while(1) {
        /* skip blanks */
        /*跳过空格*/
        while(*p && isspace(*p)) p++;
        if (*p) {
            /* get a token */
            int inq=0;  /* set to 1 if we are in "quotes" */
            int insq=0; /* set to 1 if we are in 'single quotes' */
            int done=0;

            if (current == NULL) current = sdsempty();
            while(!done) {
                if (inq) {
                    if (*p == '\\' && *(p+1) == 'x' &&
                                             is_hex_digit(*(p+2)) &&
                                             is_hex_digit(*(p+3)))
                    {
                        unsigned char byte;

                        //转换成int
                        byte = (hex_digit_to_int(*(p+2))*16)+
                                hex_digit_to_int(*(p+3));
                        //连接这个byte
                        current = sdscatlen(current,(char*)&byte,1);
                        p += 3;
                    } else if (*p == '\\' && *(p+1)) {
                        char c;

                        p++;
                        switch(*p) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'b': c = '\b'; break;
                        case 'a': c = '\a'; break;
                        default: c = *p; break;
                        }
                        current = sdscatlen(current,&c,1);
                    } else if (*p == '"') {
                        //因为到这里的时候说明前面已经有了一个双引号了
                        //所以这里说是closing quote 必须接空格或者空
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        //没有结束的双引号
                        /* unterminated quotes */
                        goto err;
                    } else {
                        //到这里可以追加了
                        current = sdscatlen(current,p,1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p+1) == '\'') {
                        p++;
                        current = sdscatlen(current,"'",1);//追加单引号
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else {
                    switch(*p) {
                    case ' ':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '\0':
                        done=1;//遇到\0认为结束
                        break;
                    case '"':
                        inq=1; //双引号
                        break;
                    case '\'':
                        insq=1;//单引号
                        break;
                    default:
                        current = sdscatlen(current,p,1);
                        break;
                    }
                }
                if (*p) p++;
            }
            /* add the token to the vector */
            vector = s_realloc(vector,((*argc)+1)*sizeof(char*));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        } else {
            /* Even on empty input string return something not NULL. */
            if (vector == NULL) vector = s_malloc(sizeof(void*));
            return vector;
        }
    }

err:
    while((*argc)--)
        sdsfree(vector[*argc]);
    s_free(vector);
    if (current) sdsfree(current);
    *argc = 0;
    return NULL;
}

/* Modify the string substituting all the occurrences of the set of
 * characters specified in the 'from' string to the corresponding character
 * in the 'to' array.
 *
 * For instance: sdsmapchars(mystring, "ho", "01", 2)
 * will have the effect of turning the string "hello" into "0ell1".
 *
 * The function returns the sds string pointer, that is always the same
 * as the input pointer since no resize is needed. */
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen) {
    size_t j, i, l = sdslen(s);

    for (j = 0; j < l; j++) {
        //根据setlen 遍历
        //好像没判断setlen 是否比l小
        for (i = 0; i < setlen; i++) {
            if (s[j] == from[i]) {
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

/* Join an array of C strings using the specified separator (also a C string).
 * Returns the result as an sds string. */
sds sdsjoin(char **argv, int argc, char *sep) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscat(join, argv[j]);
        if (j != argc-1) join = sdscat(join,sep);
    }
    return join;
}

//连接一个SDS字符串数组
/* Like sdsjoin, but joins an array of SDS strings. */
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscatsds(join, argv[j]);
        if (j != argc-1) join = sdscatlen(join,sep,seplen);
    }
    return join;
}

/*
包装器到SDS使用的分配器。注意，SDS实际上只使用定义在sdsalloc.h中的宏，以避免支付函数调用的开销。
这里我们只为链接到SDS的程序定义这些包装器（如果它们想要接触SDS内部，即使它们使用不同的分配器）
*/
/* Wrappers to the allocators used by SDS. Note that SDS will actually
 * just use the macros defined into sdsalloc.h in order to avoid to pay
 * the overhead of function calls. Here we define these wrappers only for
 * the programs SDS is linked to, if they want to touch the SDS internals
 * even if they use a different allocator. */
void *sds_malloc(size_t size) { return s_malloc(size); }
void *sds_realloc(void *ptr, size_t size) { return s_realloc(ptr,size); }
void sds_free(void *ptr) { s_free(ptr); }

#if defined(SDS_TEST_MAIN)
#include <stdio.h>
#include "testhelp.h"
#include "limits.h"

#define UNUSED(x) (void)(x)
int sdsTest(void) {
    {
        sds x = sdsnew("foo"), y;

        test_cond("Create a string and obtain the length",
            sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)
            //sdsnew中会在最后插入一个\0

        sdsfree(x);
        x = sdsnewlen("foo",2);
        test_cond("Create a string with specified length",
            sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

        x = sdscat(x,"bar");
        test_cond("Strings concatenation",
            sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

        x = sdscpy(x,"a");
        test_cond("sdscpy() against an originally longer string",
            sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

        x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
        test_cond("sdscpy() against an originally shorter string",
            sdslen(x) == 33 &&
            memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

        sdsfree(x);
        x = sdscatprintf(sdsempty(),"%d",123);
        test_cond("sdscatprintf() seems working in the base case",
            sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "Hello %s World %I,%I--", "Hi!", LLONG_MIN,LLONG_MAX);
        test_cond("sdscatfmt() seems working in the base case",
            sdslen(x) == 60 &&
            memcmp(x,"--Hello Hi! World -9223372036854775808,"
                     "9223372036854775807--",60) == 0)
        printf("[%s]\n",x);

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "%u,%U--", UINT_MAX, ULLONG_MAX);
        test_cond("sdscatfmt() seems working with unsigned numbers",
            sdslen(x) == 35 &&
            memcmp(x,"--4294967295,18446744073709551615--",35) == 0)

        sdsfree(x);
        x = sdsnew(" x ");
        sdstrim(x," x");
        test_cond("sdstrim() works when all chars match",
            sdslen(x) == 0)

        sdsfree(x);
        x = sdsnew(" x ");
        sdstrim(x," ");
        test_cond("sdstrim() works when a single char remains",
            sdslen(x) == 1 && x[0] == 'x')

        sdsfree(x);
        x = sdsnew("xxciaoyyy");
        sdstrim(x,"xy");
        test_cond("sdstrim() correctly trims characters",
            sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

        y = sdsdup(x);
        sdsrange(y,1,1);
        test_cond("sdsrange(...,1,1)",
            sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,-1);
        test_cond("sdsrange(...,1,-1)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,-2,-1);
        test_cond("sdsrange(...,-2,-1)",
            sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,2,1);
        test_cond("sdsrange(...,2,1)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,100);
        test_cond("sdsrange(...,1,100)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,100,100);
        test_cond("sdsrange(...,100,100)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("foo");
        y = sdsnew("foa");
        test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("bar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("aar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) < 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnewlen("\a\n\0foo\r",7);
        y = sdscatrepr(sdsempty(),x,sdslen(x));
        test_cond("sdscatrepr(...data...)",
            memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

        {
            unsigned int oldfree;
            char *p;
            int step = 10, j, i;

            sdsfree(x);
            sdsfree(y);
            x = sdsnew("0");
            test_cond("sdsnew() free/len buffers", sdslen(x) == 1 && sdsavail(x) == 0);

            /* Run the test a few times in order to hit the first two
             * SDS header types. */
            for (i = 0; i < 10; i++) {
                int oldlen = sdslen(x);
                x = sdsMakeRoomFor(x,step);
                int type = x[-1]&SDS_TYPE_MASK;

                test_cond("sdsMakeRoomFor() len", sdslen(x) == oldlen);
                if (type != SDS_TYPE_5) {
                    test_cond("sdsMakeRoomFor() free", sdsavail(x) >= step);
                    oldfree = sdsavail(x);
                }
                p = x+oldlen;
                for (j = 0; j < step; j++) {
                    p[j] = 'A'+j;
                }
                sdsIncrLen(x,step);
            }
            test_cond("sdsMakeRoomFor() content",
                memcmp("0ABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJ",x,101) == 0);
            test_cond("sdsMakeRoomFor() final length",sdslen(x)==101);

            sdsfree(x);
        }
    }
    test_report()
    return 0;
}
#endif

#ifdef SDS_TEST_MAIN
int main(void) {
    return sdsTest();
}
#endif
