/* Drop in replacement for zmalloc.h in order to just use libc malloc without
 * any wrappering. */

//删除zmalloc.h的替换，以便在没有任何包装的情况下使用libc malloc
//https://blog.csdn.net/Fan0920/article/details/80999850
#ifndef ZMALLOC_H
#define ZMALLOC_H

#define zmalloc malloc
#define zrealloc realloc
#define zcalloc(x) calloc(x,1)
#define zfree free
#define zstrdup strdup

#endif
