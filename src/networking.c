/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "server.h"
#include <sys/uio.h>
#include <math.h>

/*
在 ‌Linux/Unix 系统‌中，属于系统级头文件（位于 sys/uio.h），用于系统调用如 readv() 和 writev()
*/

static void setProtocolError(client *c, int pos);

//为特定sds字符串返回分配器消费的大小

//这个方法是为了计算客户端输出缓存大小
/* Return the size consumed from the allocator, for the specified SDS string,
 * including internal fragmentation. This function is used in order to compute
 * the client output buffer size. */
size_t sdsZmallocSize(sds s) {
    void *sh = sdsAllocPtr(s);
    return zmalloc_size(sh);
}

/* Return the amount of memory used by the sds string at object->ptr
 * for a string object. */
size_t getStringObjectSdsUsedMemory(robj *o) {
    serverAssertWithInfo(NULL,o,o->type == OBJ_STRING);
    switch(o->encoding) {
    case OBJ_ENCODING_RAW: return sdsZmallocSize(o->ptr);
    case OBJ_ENCODING_EMBSTR: return zmalloc_size(o)-sizeof(robj); //需减去robj本身占用的内存 因为embstr 是和sds一起分配的内存
    default: return 0; /* Just integer encoding for now. */
    }
}

void *dupClientReplyValue(void *o) {
    incrRefCount((robj*)o);
    return o;
}

int listMatchObjects(void *a, void *b) {
    return equalStringObjects(a,b);
}

client *createClient(int fd) {
    client *c = zmalloc(sizeof(client));

    /*
        传递-1作为fd，可以创建一个未连接的客户端。这很有用，因为所有命令都需要执行在客户端上下文中
        当命令在其他上下文中执行时（例如Lua脚本），我们需要一个非连接的客户端
    */
    /* passing -1 as fd it is possible to create a non connected client.
     * This is useful since all the commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    if (fd != -1) {
        //不阻塞
        anetNonBlock(NULL,fd);
        //发送方会立即发送数据，无论数据包的大小如何。
        anetEnableTcpNoDelay(NULL,fd);
        /*启用keepalive*/
        if (server.tcpkeepalive)
            anetKeepAlive(NULL,fd,server.tcpkeepalive);
        //创建一个读事件
        //https://cloud.tencent.com/developer/article/1940121
        //https://zhuanlan.zhihu.com/p/355896198    

        //这里的fd是客户端的
        if (aeCreateFileEvent(server.el,fd,AE_READABLE,
            readQueryFromClient, c) == AE_ERR)
        {
            close(fd);
            zfree(c);
            return NULL;
        }
    }
    //创建client的时候 选择第一个数据库 将client的db指针指向server.db中的第一个db
    selectDb(c,0);
    c->id = server.next_client_id++;
    c->fd = fd;
    c->name = NULL;
    c->bufpos = 0;
    /*
      客户端查询缓冲区相关 
      
      clientCron中 会有 clientsCronResizeQueryBuffer 的操作
    */
    c->querybuf = sdsempty();//创建了sds字符串 initLen等于0
    c->querybuf_peak = 0; //最近（100ms或以上）查询大小的峰值
    /* 客户端查询缓冲区相关 */

    c->reqtype = 0; //请求协议类型
    c->argc = 0;
    c->argv = NULL;
    c->cmd = c->lastcmd = NULL;
    c->multibulklen = 0;
    c->bulklen = -1;
    c->sentlen = 0;
    c->flags = 0;
    c->ctime = c->lastinteraction = server.unixtime; //当前时间
    c->authenticated = 0;
    c->replstate = REPL_STATE_NONE;
    c->repl_put_online_on_ack = 0;
    c->reploff = 0;
    c->repl_ack_off = 0;
    c->repl_ack_time = 0;
    c->slave_listening_port = 0;
    c->slave_ip[0] = '\0';
    c->slave_capa = SLAVE_CAPA_NONE;
    c->reply = listCreate();
    c->reply_bytes = 0;
    c->obuf_soft_limit_reached_time = 0;
    listSetFreeMethod(c->reply,decrRefCountVoid);
    listSetDupMethod(c->reply,dupClientReplyValue);
    c->btype = BLOCKED_NONE;
    c->bpop.timeout = 0;
    c->bpop.keys = dictCreate(&setDictType,NULL); /* 7.0变为objectKeyHeapPointerValueDictType */
    c->bpop.target = NULL;
    c->bpop.numreplicas = 0;
    c->bpop.reploffset = 0;
    c->woff = 0;
    c->watched_keys = listCreate();
    c->pubsub_channels = dictCreate(&setDictType,NULL);
    c->pubsub_patterns = listCreate();
    c->peerid = NULL;
    listSetFreeMethod(c->pubsub_patterns,decrRefCountVoid);
    listSetMatchMethod(c->pubsub_patterns,listMatchObjects);

    //当前客户端添加到队列末尾
    if (fd != -1) listAddNodeTail(server.clients,c);

    //初始化事务状态
    initClientMultiState(c);
    return c;
}

/*
    这个函数在我们将要传新数据给客户端的时候调用

    如果客户端应该收到新数据，方法就返回C_OK
    并且确保在我们的事件循环中安装写处理器，这样socket可写的时候 新的数据会被写

    如果客户端不应该收到新的消息，因为她是一个假客户端（用于在内存中加载AOF）
    一个master或者因为write处理器安装失败，函数返回C_ERR

    在以下情况下，函数可以返回C_OK，而无需实际安装写事件处理程序:
    1.事件处理程序应该已经安装，因为输出缓冲区已经包含了一些内容
    2. 客户端是一个从属服务器，但尚未联机，因此我们只想累积写入缓冲区，但尚未实际发送

    通常在每次构建应答时调用，然后将更多数据添加到客户端输出缓冲区
    如果函数返回C_ERR，则不应向输出缓冲区添加数据
*/
/* This function is called every time we are going to transmit new data
 * to the client. The behavior is the following:
 *
 * If the client should receive new data (normal clients will) the function
 * returns C_OK, and make sure to install the write handler in our event
 * loop so that when the socket is writable new data gets written.
 *
 * If the client should not receive new data, because it is a fake client
 * (used to load AOF in memory), a master or because the setup of the write
 * handler failed, the function returns C_ERR.
 *
 * The function may return C_OK without actually installing the write
 * event handler in the following cases:
 *
 * 1) The event handler should already be installed since the output buffer
 *    already contained something.
 * 客户端是一个从属服务器，但尚未联机，因此我们只想累积写入缓冲区，但尚未实际发送
 * 2) The client is a slave but not yet online, so we want to just accumulate
 *    writes in the buffer but not actually sending them yet.
 *
 * Typically gets called every time a reply is built, before adding more
 * data to the clients output buffers. If the function returns C_ERR no
 * data should be appended to the output buffers. */
int prepareClientToWrite(client *c) {

    /*如果是lua客户端 直接返回ok*/
    /* If it's the Lua client we always return ok without installing any
     * handler since there is no socket at all. */
    if (c->flags & CLIENT_LUA) return C_OK;

    /*客户端 关闭reply 或者 跳过处理，那就不要发送回复消息 */
    /*
    CLIENT REPLY SKIP  # 开启 SKIP 模式
    SET key value      # 该命令无响应
    GET key            # 此处恢复响应，返回 "value"
    */
    /* CLIENT REPLY OFF / SKIP handling: don't send replies. */
    if (c->flags & (CLIENT_REPLY_OFF|CLIENT_REPLY_SKIP)) return C_ERR;

    /*主 不接受回复，除非 CLIENT_MASTER_FORCE_REPLY 标记 被设置*/
    /* Masters don't receive replies, unless CLIENT_MASTER_FORCE_REPLY flag
     * is set. */
    if ((c->flags & CLIENT_MASTER) &&
        !(c->flags & CLIENT_MASTER_FORCE_REPLY)) return C_ERR;

    /*假客户端 比如AOF加载*/
    if (c->fd <= 0) return C_ERR; /* Fake client for AOF loading. */

    //安排 客户端仅在尚未完成（没有挂起的写入，客户端尚未标记）时将输出缓冲区写入套接字

    //对于slave来说 如果slave 在这个阶段能真正接收写入
    /* Schedule the client to write the output buffers to the socket only
     * if not already done (there were no pending writes already and the client
     * was yet not flagged), and, for slaves, if the slave can actually
     * receive writes at this stage. */
    if (!clientHasPendingReplies(c) &&
        !(c->flags & CLIENT_PENDING_WRITE) &&
        (c->replstate == REPL_STATE_NONE ||
         (c->replstate == SLAVE_STATE_ONLINE && !c->repl_put_online_on_ack)))
    {
        /*
        代替 安装写事件，
        这样，在重新进入事件循环之前，我们可以尝试直接写入客户端套接字，避免系统调用
        只有当我们不能一次写入整个回复时，我们才会真正安装写处理程序
        */
        /* Here instead of installing the write handler, we just flag the
         * client and put it into a list of clients that have something
         * to write to the socket. This way before re-entering the event
         * loop, we can try to directly write to the client sockets avoiding
         * a system call. We'll only really install the write handler if
         * we'll not be able to write the whole reply at once. */
        c->flags |= CLIENT_PENDING_WRITE;
        //添加到头部
        listAddNodeHead(server.clients_pending_write,c);
    }

    /*授权调用方在此客户机的输出缓冲区中排队*/
    /* Authorize the caller to queue in the output buffer of this client. */
    return C_OK;
}

/* Create a duplicate of the last object in the reply list when
 * it is not exclusively owned by the reply list. */
robj *dupLastObjectIfNeeded(list *reply) {
    robj *new, *cur;
    listNode *ln;
    serverAssert(listLength(reply) > 0);
    ln = listLast(reply);
    cur = listNodeValue(ln);
    if (cur->refcount > 1) {
        new = dupStringObject(cur);
        decrRefCount(cur);
        listNodeValue(ln) = new;
    }
    return listNodeValue(ln);
}

/* -----------------------------------------------------------------------------
 * Low level functions to add more data to output buffers.
 * -------------------------------------------------------------------------- */

int _addReplyToBuffer(client *c, const char *s, size_t len) {
    size_t available = sizeof(c->buf)-c->bufpos;

    //如果标记了CLIENT_CLOSE_AFTER_REPLY 就直接返回
    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return C_OK;

    /*
      已经有条目在reply列表中，我们就不能添加任何东西到静态缓冲中了
    */
    /* If there already are entries in the reply list, we cannot
     * add anything more to the static buffer. */
    if (listLength(c->reply) > 0) return C_ERR;

    /*确保缓存 有足够的 空间 被写入*/
    /* Check that the buffer has enough space available for this string. */
    if (len > available) return C_ERR;
    //memcpy严格根据第三个参数指定的‌字节数‌进行复制，不主动检查或处理\0字符
    memcpy(c->buf+c->bufpos,s,len);

    //增加位置
    c->bufpos+=len;
    return C_OK;
}

void _addReplyObjectToList(client *c, robj *o) {
    robj *tail;

    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return;

    if (listLength(c->reply) == 0) {
        incrRefCount(o);
        /*添加到tail节点*/
        listAddNodeTail(c->reply,o);
        /*内部根据 raw 和embstr 两种 算出sds使用的内存 */

        /* addReply(c,shared.mbulkhdr[ll]); 比如这里就是用的共享的对象 所以 如果两个客户端共享一个对象的话，那么两个客户端的reply_bytes内存总量肯定多出来了*/
        c->reply_bytes += getStringObjectSdsUsedMemory(o);
    } else {
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        /*PROTO_REPLY_CHUNK_BYTES = 16 kb  16*1024个字节*/
        if (tail->ptr != NULL &&
            tail->encoding == OBJ_ENCODING_RAW &&
            sdslen(tail->ptr)+sdslen(o->ptr) <= PROTO_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= sdsZmallocSize(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            tail->ptr = sdscatlen(tail->ptr,o->ptr,sdslen(o->ptr));
            c->reply_bytes += sdsZmallocSize(tail->ptr);
        } else {
            incrRefCount(o);
            listAddNodeTail(c->reply,o);
            c->reply_bytes += getStringObjectSdsUsedMemory(o);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* This method takes responsibility over the sds. When it is no longer
 * needed it will be free'd, otherwise it ends up in a robj. */
void _addReplySdsToList(client *c, sds s) {
    robj *tail;

    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
        sdsfree(s);
        return;
    }

    if (listLength(c->reply) == 0) {
        listAddNodeTail(c->reply,createObject(OBJ_STRING,s));
        c->reply_bytes += sdsZmallocSize(s);
    } else {
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        if (tail->ptr != NULL && tail->encoding == OBJ_ENCODING_RAW &&
            sdslen(tail->ptr)+sdslen(s) <= PROTO_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= sdsZmallocSize(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            tail->ptr = sdscatlen(tail->ptr,s,sdslen(s));
            c->reply_bytes += sdsZmallocSize(tail->ptr);
            sdsfree(s);
        } else {
            listAddNodeTail(c->reply,createObject(OBJ_STRING,s));
            c->reply_bytes += sdsZmallocSize(s);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

void _addReplyStringToList(client *c, const char *s, size_t len) {
    robj *tail;
    //如果标记了CLIENT_CLOSE_AFTER_REPLY 直接返回
    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return;

    if (listLength(c->reply) == 0) {
        robj *o = createStringObject(s,len);

        listAddNodeTail(c->reply,o);
        c->reply_bytes += getStringObjectSdsUsedMemory(o);
    } else {
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        if (tail->ptr != NULL && tail->encoding == OBJ_ENCODING_RAW &&
            sdslen(tail->ptr)+len <= PROTO_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= sdsZmallocSize(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            tail->ptr = sdscatlen(tail->ptr,s,len);
            c->reply_bytes += sdsZmallocSize(tail->ptr);
        } else {
            robj *o = createStringObject(s,len);

            //添加到reply列表结尾
            listAddNodeTail(c->reply,o);
            c->reply_bytes += getStringObjectSdsUsedMemory(o);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* -----------------------------------------------------------------------------

    高层方法：在客户端输出缓冲区数据排队
 * Higher level functions to queue data on the client output buffer.
 * The following functions are the ones that commands implementations will call.
 * -------------------------------------------------------------------------- */

/*
用于在客户机输出缓冲区上对数据进行排队的高级函数
以下函数是命令实现将调用的函数
*/
void addReply(client *c, robj *obj) {
    //函数返回false的情况直接返回
    if (prepareClientToWrite(c) != C_OK) return;

    /*
     这是一个重要的地方，当有一个保存子进程在运行时，我们可以避免写时复制，避免在不需要时触及对象的refcount字段
     如果编码是RAW，并且静态缓冲区中有空间，我们将能够将对象发送到客户端，而不会干扰其页面
    */
    /* This is an important place where we can avoid copy-on-write
     * when there is a saving child running, avoiding touching the
     * refcount field of the object if it's not needed.
     *
     * If the encoding is RAW and there is room in the static buffer
     * we'll be able to send the object to the client without
     * messing with its page. */
    /*raw 或者embstr 编码*/
    if (sdsEncodedObject(obj)) {
        //先尝试添加到缓冲区 //内部使用memcpy
        //如果缓冲区 添加不成功 才会把对象添加到c->reply中
        /*如果c->reply 已经有节点了 也不会添加到c->buf中 */
        if (_addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != C_OK)
            _addReplyObjectToList(c,obj);
    } else if (obj->encoding == OBJ_ENCODING_INT) {
        /*如果编码是数字*/

        /* Optimization: if there is room in the static buffer for 32 bytes
         * (more than the max chars a 64 bit integer can take as string) we
         * avoid decoding the object and go for the lower level approach. */
        if (listLength(c->reply) == 0 && (sizeof(c->buf) - c->bufpos) >= 32) {
            char buf[32];
            int len;

            len = ll2string(buf,sizeof(buf),(long)obj->ptr);
            if (_addReplyToBuffer(c,buf,len) == C_OK)
                return;
            /* else... continue with the normal code path, but should never
             * happen actually since we verified there is room. */
        }
        //返回的是解码过后的对象
        obj = getDecodedObject(obj);
        //添加到客户端的buf缓冲区中 
        //如果缓冲区 添加不成功 才会把对象添加到c->reply中
        if (_addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != C_OK)
            _addReplyObjectToList(c,obj);

        //减少引用计数
        decrRefCount(obj);
    } else {
        serverPanic("Wrong obj->encoding in addReply()");
    }
}

void addReplySds(client *c, sds s) {
    if (prepareClientToWrite(c) != C_OK) {
        /* The caller expects the sds to be free'd. */
        sdsfree(s);
        return;
    }
    if (_addReplyToBuffer(c,s,sdslen(s)) == C_OK) {
        sdsfree(s);
    } else {
        /* This method free's the sds when it is no longer needed. */
        _addReplySdsToList(c,s);
    }
}

void addReplyString(client *c, const char *s, size_t len) {
    if (prepareClientToWrite(c) != C_OK) return;
    if (_addReplyToBuffer(c,s,len) != C_OK)
        _addReplyStringToList(c,s,len);
}

void addReplyErrorLength(client *c, const char *s, size_t len) {
    addReplyString(c,"-ERR ",5);
    addReplyString(c,s,len);
    addReplyString(c,"\r\n",2);
}

void addReplyError(client *c, const char *err) {
    addReplyErrorLength(c,err,strlen(err));
}

void addReplyErrorFormat(client *c, const char *fmt, ...) {
    size_t l, j;
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    /* Make sure there are no newlines in the string, otherwise invalid protocol
     * is emitted. */
    l = sdslen(s);
    for (j = 0; j < l; j++) {
        if (s[j] == '\r' || s[j] == '\n') s[j] = ' ';
    }
    addReplyErrorLength(c,s,sdslen(s));
    sdsfree(s);
}

void addReplyStatusLength(client *c, const char *s, size_t len) {
    addReplyString(c,"+",1);
    addReplyString(c,s,len);
    addReplyString(c,"\r\n",2);
}

void addReplyStatus(client *c, const char *status) {
    addReplyStatusLength(c,status,strlen(status));
}

void addReplyStatusFormat(client *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    addReplyStatusLength(c,s,sdslen(s));
    sdsfree(s);
}

/* Adds an empty object to the reply list that will contain the multi bulk
 * length, which is not known when this function is called. */
void *addDeferredMultiBulkLength(client *c) {
    /* Note that we install the write event here even if the object is not
     * ready to be sent, since we are sure that before returning to the
     * event loop setDeferredMultiBulkLength() will be called. */
    if (prepareClientToWrite(c) != C_OK) return NULL;
    /*加入到链表最后*/
    listAddNodeTail(c->reply,createObject(OBJ_STRING,NULL));
    return listLast(c->reply);
}

/* Populate the length object and try gluing it to the next chunk. */
void setDeferredMultiBulkLength(client *c, void *node, long length) {
    listNode *ln = (listNode*)node;
    robj *len, *next;

    /* Abort when *node is NULL (see addDeferredMultiBulkLength). */
    if (node == NULL) return;

    len = listNodeValue(ln);
    len->ptr = sdscatprintf(sdsempty(),"*%ld\r\n",length);
    len->encoding = OBJ_ENCODING_RAW; /* in case it was an EMBSTR. */
    c->reply_bytes += sdsZmallocSize(len->ptr);
    /*如果当前节点还有下个元素 那么就一起*/
    if (ln->next != NULL) {
        next = listNodeValue(ln->next);

        /* Only glue when the next node is non-NULL (an sds in this case) */
        if (next->ptr != NULL) {
            c->reply_bytes -= sdsZmallocSize(len->ptr);
            c->reply_bytes -= getStringObjectSdsUsedMemory(next);
            len->ptr = sdscatlen(len->ptr,next->ptr,sdslen(next->ptr));
            c->reply_bytes += sdsZmallocSize(len->ptr);
            listDelNode(c->reply,ln->next);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* Add a double as a bulk reply */
void addReplyDouble(client *c, double d) {
    char dbuf[128], sbuf[128];
    int dlen, slen;
    if (isinf(d)) {
        /* Libc in odd systems (Hi Solaris!) will format infinite in a
         * different way, so better to handle it in an explicit way. */
        addReplyBulkCString(c, d > 0 ? "inf" : "-inf");
    } else {
        dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);
        slen = snprintf(sbuf,sizeof(sbuf),"$%d\r\n%s\r\n",dlen,dbuf);
        addReplyString(c,sbuf,slen);
    }
}

/* Add a long double as a bulk reply, but uses a human readable formatting
 * of the double instead of exposing the crude behavior of doubles to the
 * dear user. */
void addReplyHumanLongDouble(client *c, long double d) {
    robj *o = createStringObjectFromLongDouble(d,1);
    addReplyBulk(c,o);
    decrRefCount(o);
}

/* Add a long long as integer reply or bulk len / multi bulk count.
 * Basically this is used to output <prefix><long long><crlf>. */
void addReplyLongLongWithPrefix(client *c, long long ll, char prefix) {
    char buf[128];
    int len;

    /* Things like $3\r\n or *2\r\n are emitted very often by the protocol
     * so we have a few shared objects to use if the integer is small
     * like it is most of the times. */
    if (prefix == '*' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        addReply(c,shared.mbulkhdr[ll]);
        return;
    } else if (prefix == '$' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        addReply(c,shared.bulkhdr[ll]);
        return;
    }

    buf[0] = prefix;
    len = ll2string(buf+1,sizeof(buf)-1,ll);
    buf[len+1] = '\r';
    buf[len+2] = '\n';
    addReplyString(c,buf,len+3);
}

void addReplyLongLong(client *c, long long ll) {
    if (ll == 0)
        addReply(c,shared.czero);
    else if (ll == 1)
        addReply(c,shared.cone);
    else
        addReplyLongLongWithPrefix(c,ll,':');
}

void addReplyMultiBulkLen(client *c, long length) {
    if (length < OBJ_SHARED_BULKHDR_LEN)
        addReply(c,shared.mbulkhdr[length]);
    else
        addReplyLongLongWithPrefix(c,length,'*');
}

/* Create the length prefix of a bulk reply, example: $2234 */
void addReplyBulkLen(client *c, robj *obj) {
    size_t len;

    if (sdsEncodedObject(obj)) {
        len = sdslen(obj->ptr);
    } else {
        long n = (long)obj->ptr;

        /* Compute how many bytes will take this integer as a radix 10 string */
        len = 1;
        if (n < 0) {
            len++;
            n = -n;
        }
        while((n = n/10) != 0) {
            len++;
        }
    }

    //小于32长度 直接使用共享变量
    if (len < OBJ_SHARED_BULKHDR_LEN)
        addReply(c,shared.bulkhdr[len]);
    else
        addReplyLongLongWithPrefix(c,len,'$');
}

/* Add a Redis Object as a bulk reply */
void addReplyBulk(client *c, robj *obj) {
    addReplyBulkLen(c,obj);
    addReply(c,obj);
    // \r\n
    addReply(c,shared.crlf);
}

/* Add a C buffer as bulk reply */
void addReplyBulkCBuffer(client *c, const void *p, size_t len) {
    addReplyLongLongWithPrefix(c,len,'$');
    addReplyString(c,p,len);
    addReply(c,shared.crlf);
}

/* Add sds to reply (takes ownership of sds and frees it) */
void addReplyBulkSds(client *c, sds s)  {
    addReplySds(c,sdscatfmt(sdsempty(),"$%u\r\n",
        (unsigned long)sdslen(s)));
    addReplySds(c,s);
    addReply(c,shared.crlf);
}

/* Add a C nul term string as bulk reply */
void addReplyBulkCString(client *c, const char *s) {
    if (s == NULL) {
        addReply(c,shared.nullbulk);
    } else {
        addReplyBulkCBuffer(c,s,strlen(s));
    }
}

/* Add a long long as a bulk reply */
void addReplyBulkLongLong(client *c, long long ll) {
    char buf[64];
    int len;

    len = ll2string(buf,64,ll);
    addReplyBulkCBuffer(c,buf,len);
}

/* Copy 'src' client output buffers into 'dst' client output buffers.
 * The function takes care of freeing the old output buffers of the
 * destination client. */
void copyClientOutputBuffer(client *dst, client *src) {
    listRelease(dst->reply);
    dst->reply = listDup(src->reply);
    memcpy(dst->buf,src->buf,src->bufpos);
    dst->bufpos = src->bufpos;
    dst->reply_bytes = src->reply_bytes;
}

/* Return true if the specified client has pending reply buffers to write to
 * the socket. */
int clientHasPendingReplies(client *c) {
    return c->bufpos || listLength(c->reply);
}

#define MAX_ACCEPTS_PER_CALL 1000
static void acceptCommonHandler(int fd, int flags, char *ip) {
    client *c;
    //创建客户端 fd为当前客户端fd
    //内部会创建 readQueryFromClient事件
    if ((c = createClient(fd)) == NULL) {
        serverLog(LL_WARNING,
            "Error registering fd event for the new client: %s (fd=%d)",
            strerror(errno),fd);
        close(fd); /* May be already closed, just ignore errors */
        return;
    }
    /*
     如果最大客户端指令被设置了 并且这个又是另外一个客户端，关闭连接。
     注意我们创建客户端而不在这个条件之前检查，是由于socket已经被设置为不阻塞模式，
     我们能够发送错误

    */
    /* If maxclient directive is set and this is one client more... close the
     * connection. Note that we create the client instead to check before
     * for this condition, since now the socket is already set in non-blocking
     * mode and we can send an error for free using the Kernel I/O */
    if (listLength(server.clients) > server.maxclients) {
        char *err = "-ERR max number of clients reached\r\n";

        /* That's a best effort error message, don't check write errors */
        if (write(c->fd,err,strlen(err)) == -1) {
            /* 不做任何事，只是阻止警告 */
            /* Nothing to do, Just to avoid the warning... */
        }
        //拒绝数增加
        server.stat_rejected_conn++;
        /*释放客户端*/
        freeClient(c);
        return;
    }

    /*
    我们不接受非环回接口的请求

    保护模式开启后 如果是tcp连接那么 绑定地址 或者 密码不能为空

    bindaddr_count 是在配置文件中配置bind 地址的数量 
    */
    /* If the server is running in protected mode (the default) and there
     * is no password set, nor a specific interface is bound, we don't accept
     * requests from non loopback interfaces. Instead we try to explain the
     * user what to do to fix it if needed. */
    if (server.protected_mode &&
        server.bindaddr_count == 0 &&
        server.requirepass == NULL &&
        !(flags & CLIENT_UNIX_SOCKET) &&
        ip != NULL)
    {
        //既不等于127.0.0.1 又不等于::1
        if (strcmp(ip,"127.0.0.1") && strcmp(ip,"::1")) {
            //阻止redis运行在保护模式下是因为保护模式开启了，没有指定绑定地址，没有验证密码给到客户端。
            //在这种模式下连接只会被回环接口接受

            //如果你想从外部电脑连接到redis 你可以适配以下方案中的一个
            //1. 从同一个host连接redis服务器 发送CONFIG SET protected-mode no 禁用保护模式
            // 确保redis不能从外网访问。使用config rewrite 使这个配置永久

            //2. 或者你可以通过编辑Redis配置文件禁用保护模式，并将保护模式选项设置为“no”，然后重新启动服务器

            //3.如果您手动启动服务器只是为了测试，请使用“——protected-mode no”选项重新启动它

            //4. 设置绑定地址或身份验证密码

            //注意：你只需要做上面的一件事就可以服务器开始接受来自外部的连接
            char *err =
                "-DENIED Redis is running in protected mode because protected "
                "mode is enabled, no bind address was specified, no "
                "authentication password is requested to clients. In this mode "
                "connections are only accepted from the loopback interface. "
                "If you want to connect from external computers to Redis you "
                "may adopt one of the following solutions: "
                "1) Just disable protected mode sending the command "
                "'CONFIG SET protected-mode no' from the loopback interface "
                "by connecting to Redis from the same host the server is "
                "running, however MAKE SURE Redis is not publicly accessible "
                "from internet if you do so. Use CONFIG REWRITE to make this "
                "change permanent. "
                "2) Alternatively you can just disable the protected mode by "
                "editing the Redis configuration file, and setting the protected "
                "mode option to 'no', and then restarting the server. "
                "3) If you started the server manually just for testing, restart "
                "it with the '--protected-mode no' option. "
                "4) Setup a bind address or an authentication password. "
                "NOTE: You only need to do one of the above things in order for "
                "the server to start accepting connections from the outside.\r\n";
            if (write(c->fd,err,strlen(err)) == -1) {
                /* Nothing to do, Just to avoid the warning... */
            }
            server.stat_rejected_conn++;
            freeClient(c);
            return;
        }
    }
    //接收连接的数量增加
    server.stat_numconnections++;
    c->flags |= flags;
}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    
    //参数fd是服务器创建的socket 文件描述符
    
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL; //1000
    char cip[NET_IP_STR_LEN]; //NET_IP_STR_LEN = 46
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    /* 1.3.6 是while(1)*/
    /*应该是同时可以接收多少客户端连接 */
    /*如果一直循环 那么 其他事件可能会阻塞*/
    while(max--) {
        //cfd 是客户端的文件描述符
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);

        /*
          在listenToPort 函数中设置了非阻塞
        */

        //默认情况下，如果套接字是阻塞模式，accept会一直阻塞直到有新的连接到达。
        //而如果套接字被设置为非阻塞模式，accept在没有连接时会立即返回错误，比如EAGAIN或EWOULDBLOCK。
        //所以这边没有连接的时候会立即返回 并且错误码是EWOULDBLOCK
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
           //https://ytcoode.io/article/too-many-open-files-%E9%94%99%E8%AF%AF%E5%AF%BC%E8%87%B4%E6%9C%8D%E5%8A%A1%E5%99%A8%E6%AD%BB%E5%BE%AA%E7%8E%AF/index.html
            return;
        }
        serverLog(LL_VERBOSE,"Accepted %s:%d", cip, cport);
        /*
          内部会 创建客户端
        */
        acceptCommonHandler(cfd,0,cip);
    }
}

void acceptUnixHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cfd, max = MAX_ACCEPTS_PER_CALL;
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    while(max--) {
        cfd = anetUnixAccept(server.neterr, fd);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }
        serverLog(LL_VERBOSE,"Accepted connection to %s", server.unixsocket);
        acceptCommonHandler(cfd,CLIENT_UNIX_SOCKET,NULL);
    }
}

static void freeClientArgv(client *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        decrRefCount(c->argv[j]);
    //参数数量重置为0
    c->argc = 0;
    c->cmd = NULL;
}

/* Close all the slaves connections. This is useful in chained replication
 * when we resync with our own master and want to force all our slaves to
 * resync with us as well. */
void disconnectSlaves(void) {
    while (listLength(server.slaves)) {
        listNode *ln = listFirst(server.slaves);
        freeClient((client*)ln->value);
    }
}

/* Remove the specified client from global lists where the client could
 * be referenced, not including the Pub/Sub channels.
 * This is used by freeClient() and replicationCacheMaster(). */
void unlinkClient(client *c) {
    listNode *ln;

    /* If this is marked as current client unset it. */
    if (server.current_client == c) server.current_client = NULL;

    /* Certain operations must be done only if the client has an active socket.
     * If the client was already unlinked or if it's a "fake client" the
     * fd is already set to -1. */
    if (c->fd != -1) {
        /* Remove from the list of active clients. */
        ln = listSearchKey(server.clients,c);
        serverAssert(ln != NULL);
        listDelNode(server.clients,ln);

        /* Unregister async I/O handlers and close the socket. */
        aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
        close(c->fd);
        c->fd = -1;
    }

    /* Remove from the list of pending writes if needed. */
    if (c->flags & CLIENT_PENDING_WRITE) {
        ln = listSearchKey(server.clients_pending_write,c);
        serverAssert(ln != NULL);
        listDelNode(server.clients_pending_write,ln);
        c->flags &= ~CLIENT_PENDING_WRITE;
    }

    /* When client was just unblocked because of a blocking operation,
     * remove it from the list of unblocked clients. */
    if (c->flags & CLIENT_UNBLOCKED) {
        ln = listSearchKey(server.unblocked_clients,c);
        serverAssert(ln != NULL);
        listDelNode(server.unblocked_clients,ln);
        c->flags &= ~CLIENT_UNBLOCKED;
    }
}

void freeClient(client *c) {
    listNode *ln;

    /*如果客户端是master */
    /* If it is our master that's beging disconnected we should make sure
     * to cache the state to try a partial resynchronization later.
     *
     * Note that before doing this we make sure that the client is not in
     * some unexpected state, by checking its flags. */
    if (server.master && c->flags & CLIENT_MASTER) {
        
        serverLog(LL_WARNING,"Connection with master lost.");


        if (!(c->flags & (CLIENT_CLOSE_AFTER_REPLY|
                          CLIENT_CLOSE_ASAP|
                          CLIENT_BLOCKED|
                          CLIENT_UNBLOCKED)))
        {
            //把master缓存起来，用于之后的部分同步
            //内部也会调用 replicationHandleMasterDisconnection 
            //置为REPL_STATE_CONNECT状态
            replicationCacheMaster(c);
            return;
        }
    }

    /* Log link disconnection with slave */
    if ((c->flags & CLIENT_SLAVE) && !(c->flags & CLIENT_MONITOR)) {
        serverLog(LL_WARNING,"Connection with slave %s lost.",
            replicationGetSlaveName(c));
    }

    /* Free the query buffer */
    sdsfree(c->querybuf);
    c->querybuf = NULL;

    /* Deallocate structures used to block on blocking ops. */
    if (c->flags & CLIENT_BLOCKED) unblockClient(c);
    dictRelease(c->bpop.keys);

    /* UNWATCH all the keys */
    unwatchAllKeys(c);
    listRelease(c->watched_keys);

    /* Unsubscribe from all the pubsub channels */
    pubsubUnsubscribeAllChannels(c,0);
    pubsubUnsubscribeAllPatterns(c,0);
    dictRelease(c->pubsub_channels);
    listRelease(c->pubsub_patterns);

    /* Free data structures. */
    listRelease(c->reply);
    freeClientArgv(c);

    /* Unlink the client: this will close the socket, remove the I/O
     * handlers, and remove references of the client from different
     * places where active clients may be referenced. */
    unlinkClient(c);

    /* Master/slave cleanup Case 1:
     * we lost the connection with a slave. */
    if (c->flags & CLIENT_SLAVE) {
        if (c->replstate == SLAVE_STATE_SEND_BULK) {
            if (c->repldbfd != -1) close(c->repldbfd);
            if (c->replpreamble) sdsfree(c->replpreamble);
        }
        list *l = (c->flags & CLIENT_MONITOR) ? server.monitors : server.slaves;
        ln = listSearchKey(l,c);
        serverAssert(ln != NULL);
        listDelNode(l,ln);
        /* We need to remember the time when we started to have zero
         * attached slaves, as after some time we'll free the replication
         * backlog. */
        if (c->flags & CLIENT_SLAVE && listLength(server.slaves) == 0)
            server.repl_no_slaves_since = server.unixtime;
        refreshGoodSlavesCount();
    }

    /* Master/slave cleanup Case 2:
     * we lost the connection with the master. */
    if (c->flags & CLIENT_MASTER) replicationHandleMasterDisconnection();

    /* If this client was scheduled for async freeing we need to remove it
     * from the queue. */
    if (c->flags & CLIENT_CLOSE_ASAP) {
        ln = listSearchKey(server.clients_to_close,c);
        serverAssert(ln != NULL);
        listDelNode(server.clients_to_close,ln);
    }

    /* Release other dynamically allocated client structure fields,
     * and finally release the client structure itself. */
    if (c->name) decrRefCount(c->name);
    zfree(c->argv);
    freeClientMultiState(c);
    sdsfree(c->peerid);
    zfree(c);
}

/* Schedule a client to free it at a safe time in the serverCron() function.
 * This function is useful when we need to terminate a client but we are in
 * a context where calling freeClient() is not possible, because the client
 * should be valid for the continuation of the flow of the program. */
void freeClientAsync(client *c) {
    if (c->flags & CLIENT_CLOSE_ASAP || c->flags & CLIENT_LUA) return;
    c->flags |= CLIENT_CLOSE_ASAP;
    listAddNodeTail(server.clients_to_close,c);
}

void freeClientsInAsyncFreeQueue(void) {
    while (listLength(server.clients_to_close)) {
        listNode *ln = listFirst(server.clients_to_close);
        client *c = listNodeValue(ln);

        c->flags &= ~CLIENT_CLOSE_ASAP;
        freeClient(c);
        listDelNode(server.clients_to_close,ln);
    }
}

/* Write data in output buffers to client. Return C_OK if the client
 * is still valid after the call, C_ERR if it was freed. */
int writeToClient(int fd, client *c, int handler_installed) {
    ssize_t nwritten = 0, totwritten = 0;
    size_t objlen;
    size_t objmem;
    robj *o;

    //有挂起的回复消息时
    // bufpos 大于0 或者 reply列表有对象时
    while(clientHasPendingReplies(c)) {

        //判断bufpos 是因为好判断 真正是在buf里
        if (c->bufpos > 0) {
            //直接写
            // //写回客户端
            nwritten = write(fd,c->buf+c->sentlen,c->bufpos-c->sentlen);
            if (nwritten <= 0) break;
            c->sentlen += nwritten;
            totwritten += nwritten;

            /* If the buffer was sent, set bufpos to zero to continue with
             * the remainder of the reply. */
            if ((int)c->sentlen == c->bufpos) {
                c->bufpos = 0;
                c->sentlen = 0;
            }
        } else {
            o = listNodeValue(listFirst(c->reply));
            objlen = sdslen(o->ptr);
            objmem = getStringObjectSdsUsedMemory(o);

            if (objlen == 0) {
                listDelNode(c->reply,listFirst(c->reply));
                c->reply_bytes -= objmem;
                continue;
            }
            //写回客户端
            nwritten = write(fd, ((char*)o->ptr)+c->sentlen,objlen-c->sentlen);
            if (nwritten <= 0) break;
            c->sentlen += nwritten;
            totwritten += nwritten;

            /* If we fully sent the object on head go to the next one */
            if (c->sentlen == objlen) {
                listDelNode(c->reply,listFirst(c->reply));
                c->sentlen = 0;
                c->reply_bytes -= objmem;
            }
        }
        /*
        注意，我们避免发送超过NET_MAX_WRITES_PER_EVENT字节，在单线程服务器中，服务其他客户端也是一个好主意，
        即使一个非常大的请求来自总是能够接受数据的超级快速链接（在现实世界的场景中，考虑对环回接口的‘KEYS *’）

        然而，如果我们超过了最大内存限制，我们就忽略它，只是尽可能多地传递数据。

        也就是说超过64 kb 且 内存 没有达到最大内存 那就中断写

        豆包翻译：
        需要注意的是，我们会避免在单次事件中发送超过 NET_MAX_WRITES_PER_EVENT 字节的数据。
        对于单线程服务器而言，这是一项合理的设计 —— 即便某个客户端通过超高速链路（能持续接收数据）
        发送了超大请求（现实场景中可类比 “通过本地回环接口执行 KEYS * 命令” 的情况），服务器也能兼顾其他客户端的请求处理。
        */
        /* Note that we avoid to send more than NET_MAX_WRITES_PER_EVENT
         * bytes, in a single threaded server it's a good idea to serve
         * other clients as well, even if a very large request comes from
         * super fast link that is always able to accept data (in real world
         * scenario think about 'KEYS *' against the loopback interface).
         *
         * However if we are over the maxmemory limit we ignore that and
         * just deliver as much data as it is possible to deliver. */

        /*已经写回客户端的总字节数 大于 每次事件的最大写回数*/
        if (totwritten > NET_MAX_WRITES_PER_EVENT &&
            (server.maxmemory == 0 ||
             zmalloc_used_memory() < server.maxmemory)) break;
    }
    server.stat_net_output_bytes += totwritten;
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            serverLog(LL_VERBOSE,
                "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return C_ERR;
        }
    }
    if (totwritten > 0) {
        /* For clients representing masters we don't count sending data
         * as an interaction, since we always send REPLCONF ACK commands
         * that take some time to just fill the socket output buffer.
         * We just rely on data / pings received for timeout detection. */
        if (!(c->flags & CLIENT_MASTER)) c->lastinteraction = server.unixtime;
    }

    
     //没有挂起的回复时 才会有可能去删除这个写事件

     //如果还有挂起的消息需要处理 也就是不会删除事件
     //那么在 handleClientsWithPendingWrites 中有可能还会添加写事件吧？？
    if (!clientHasPendingReplies(c)) {

       

        c->sentlen = 0;

        //在 sendReplyToClient 中 会标记handler_installed 为1
        //如果安装了处理器 那么就删除它
        if (handler_installed) aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);

        /*
         如果标记了 CLIENT_CLOSE_AFTER_REPLY 那就关闭连接
        */
        /* Close connection after entire reply has been sent. */
        if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
            freeClient(c);
            return C_ERR;
        }
    }
    return C_OK;
}

/**
 * @brief 写事件处理器，只是发送数据到客户端
 * 
 */
/* Write event handler. Just send data to the client. */
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    UNUSED(el);
    UNUSED(mask);
    //写后有可能 会删除这个写事件
    writeToClient(fd,privdata,1);
}

/**
 * @brief  这个函数仅在进入事件循环前调用
 * 希望我们可以只将回复写入客户端输出缓冲区，而不需要使用系统调用来安装可写事件处理程序，调用它等等
 */
/* This function is called just before entering the event loop, in the hope
 * we can just write the replies to the client output buffer without any
 * need to use a syscall in order to install the writable event handler,
 * get it called, and so forth. */
int handleClientsWithPendingWrites(void) {
    listIter li;
    listNode *ln;
    int processed = listLength(server.clients_pending_write);

    listRewind(server.clients_pending_write,&li);
    while((ln = listNext(&li))) {

        //当前客户端
        client *c = listNodeValue(ln);
        c->flags &= ~CLIENT_PENDING_WRITE;
        //从clients_pending_write 中删除这个节点
        listDelNode(server.clients_pending_write,ln);

        /* Try to write buffers to the client socket. */
        /*内部不会删除 写事件 */

        /*内部继续循环 判断 bufpos 和 reply列表*/
        if (writeToClient(c->fd,c,0) == C_ERR) continue;

        /*
        如果在上面的同步写入之后，我们仍然有数据要输出到客户端，我们需要安装可写处理handler
        */
        /* If after the synchronous writes above we still have data to
         * output to the client, we need to install the writable handler. */
        if (clientHasPendingReplies(c)) {
            int ae_flags = AE_WRITABLE;
            /* For the fsync=always policy, we want that a given FD is never
             * served for reading and writing in the same event loop iteration,
             * so that in the middle of receiving the query, and serving it
             * to the client, we'll call beforeSleep() that will do the
             * actual fsync of AOF to disk. AE_BARRIER ensures that. */
            /**
             * 对于fsync=always策略，我们希望给定的FD永远不会在同一个事件循环迭代中用于读和写，
             * 因此在接收查询并将其提供给客户端的过程中，我们将调用beforeSleep（），它将对磁盘执行AOF的实际fsync
             * 
             *  这个意思是说读取客户端 执行命令 之后 不要再写回客户端
             * 
             *  所以 利用beforeSleep 方法 执行真正的aof同步到磁盘
            */
           /**
            * fsync=always策略是每次命令都同步写入
            * 
            * //这里为什么这样 我的理解是 
            * 因为 读事件中 执行了 命令之后 是把回复消息 写入了缓存c->buf中 不是立马写入客户端的
            * 
            * 
           */
            if (server.aof_state == AOF_ON &&
                server.aof_fsync == AOF_FSYNC_ALWAYS)
            {
                //因为在这种情况下 在这个方法执行之前 执行了 aof写入磁盘操作
                //所以必须 立马回复客户端 所以 这里要执行反转操作
                //确保 写回客户端操作在执行 读 事件之前执行

                //确保写事件在读事件之前触发
                ae_flags |= AE_BARRIER;
            }
            //注册一个写事件，写回客户端
            //注册失败 才会 异步释放这个客户端

            //sendReplyToClient 内部才会 设置 删除事件
            //这里可以防止 万一 sendReply中删除了这个事件？
            if (aeCreateFileEvent(server.el, c->fd, ae_flags,
                sendReplyToClient, c) == AE_ERR)
            {
                    freeClientAsync(c);
            }
        }
    }
    return processed;
}

/*
准备客户端处理下一个命令
*/
/* resetClient prepare the client to process the next command */
void resetClient(client *c) {
    //获取当前命令的处理函数
    redisCommandProc *prevcmd = c->cmd ? c->cmd->proc : NULL;

    /* 这边就遍历 释放 客户端参数对象了*/
    freeClientArgv(c);
    c->reqtype = 0;//清空请求类型
    c->multibulklen = 0; //多块长度清零
    c->bulklen = -1;//块长度清零

    /*
     如果我们不在MULTI中且刚刚执行的不是asking命令，我们也会清除请求标志
     asking命令用于集群
    */
    /* We clear the ASKING flag as well if we are not inside a MULTI, and
     * if what we just executed is not the ASKING command itself. */
    if (!(c->flags & CLIENT_MULTI) && prevcmd != askingCommand)
        c->flags &= ~CLIENT_ASKING;

    /*
     删除CLIENT_REPLY_SKIP标志，以便发送对下一个命令的回复
     但如果我们刚刚处理的命令是“CLIENT REPLY SKIP”(  客户端发送skip 命令)，则设置标志。
   
    */
    /* Remove the CLIENT_REPLY_SKIP flag if any so that the reply
     * to the next command will be sent, but set the flag if the command
     * we just processed was "CLIENT REPLY SKIP". */
    c->flags &= ~CLIENT_REPLY_SKIP;

    //客户端发送client reply skip的时候会设置 CLIENT_REPLY_SKIP_NEXT

    //比如客户端发送skip
    //https://redis.io/docs/latest/commands/client-reply/
    //有时，客户端完全禁用Redis服务器的回复是很有用的。
    //例如，当客户端发送“触发并忘记”命令或执行大量数据加载时，或者在不断流式传输新数据的缓存环境中。
    //在这种情况下，使用服务器时间和带宽向客户端发送应答（这些应答将被忽略）被认为是浪费。
    //CLIENT REPLY命令控制服务器是否回复客户端的命令。有以下几种模式可供选择
    //On。这是服务器对每个命令返回应答的默认模式。
    //Off。在这种模式下，服务器不会回复客户端的命令。
    //Skip。此模式在此命令后 跳过回复
    if (c->flags & CLIENT_REPLY_SKIP_NEXT) {

        //设置这个后 如果发送命令 那么就没有回复
        //因为 prepareClientToWrite 函数中判断了 
        c->flags |= CLIENT_REPLY_SKIP;

        //重置
        c->flags &= ~CLIENT_REPLY_SKIP_NEXT;
    }
}

int processInlineBuffer(client *c) {
    char *newline;
    int argc, j;
    sds *argv, aux;
    size_t querylen;

    /*查找行的结尾*/
    /* Search for end of line */
    newline = strchr(c->querybuf,'\n');

    /*如果没有\n 就直接报错*/
    /* Nothing to do without a \r\n */
    if (newline == NULL) {
        //最大缓冲区为 64 kb
        if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
            addReplyError(c,"Protocol error: too big inline request");
            setProtocolError(c,0);
        }
        return C_ERR;
    }

    /*处理\r\n*/
    /* Handle the \r\n case. */
    if (newline && newline != c->querybuf && *(newline-1) == '\r')
        newline--;

    /* Split the input buffer up to the \r\n */
    querylen = newline-(c->querybuf);

    //调用sdsnewlen后 querybuf还能使用
    aux = sdsnewlen(c->querybuf,querylen);
    
    //按空格分割字符串
    argv = sdssplitargs(aux,&argc);
    sdsfree(aux); //释放原来的sds
    if (argv == NULL) {
        addReplyError(c,"Protocol error: unbalanced quotes in request");
        setProtocolError(c,0);
        return C_ERR;
    }

    /*
        从服务 的换行符可用于刷新最后一次ACK时间
        这对于从服务器在加载大的RDB文件时回ping很有用
    */
    /* Newline from slaves can be used to refresh the last ACK time.
     * This is useful for a slave to ping back while loading a big
     * RDB file. */
    if (querylen == 0 && c->flags & CLIENT_SLAVE)
        c->repl_ack_time = server.unixtime;

    /*
      在querybuf缓冲区中保留查询第一行之后的数据
    */
    /* Leave data after the first line of the query in the buffer */
    sdsrange(c->querybuf,querylen+2,-1);

    /*在客户端结构上设置argv数组*/
    /* Setup argv array on client structure */
    if (argc) {
        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(robj*)*argc);
    }

    /*为所有参数创建redis对象*/
    /* Create redis objects for all arguments. */
    for (c->argc = 0, j = 0; j < argc; j++) {
        if (sdslen(argv[j])) {
            //放入argv 返回一个robj 指针
            c->argv[c->argc] = createObject(OBJ_STRING,argv[j]);
            c->argc++; //参数个数增加
        } else {
            sdsfree(argv[j]);
        }
    }
    //释放 argv
    zfree(argv);
    return C_OK;
}

/* Helper function. Trims query buffer to make the function that processes
 * multi bulk requests idempotent. */
static void setProtocolError(client *c, int pos) {
    if (server.verbosity <= LL_VERBOSE) {
        sds client = catClientInfoString(sdsempty(),c);
        serverLog(LL_VERBOSE,
            "Protocol error from client: %s", client);
        sdsfree(client);
    }
    c->flags |= CLIENT_CLOSE_AFTER_REPLY;
    sdsrange(c->querybuf,pos,-1);
}

int processMultibulkBuffer(client *c) {
    char *newline = NULL;
    int pos = 0, ok;
    long long ll;

    if (c->multibulklen == 0) {
        /* The client should have been reset */
        /*意思就是c->argc 应该为0*/
        /*在freeClientArgv 中重置为0*/
        serverAssertWithInfo(c,NULL,c->argc == 0);

        // 多块的长度 不可能没有\r\n
        /* Multi bulk length cannot be read without a \r\n */
        newline = strchr(c->querybuf,'\r');
        if (newline == NULL) {
            //64kb 也就是查询缓冲区不能大于64kb
            //竟然在newline为空的时候去判断
            if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
                addReplyError(c,"Protocol error: too big mbulk count string");
                setProtocolError(c,0);
            }
            return C_ERR;
        }

        /*缓冲区 也应该包含\n */
        /* Buffer should also contain \n */
        if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
            return C_ERR; //-1

        /* We know for sure there is a whole line since newline != NULL,
         * so go ahead and find out the multi bulk length. */
        /*c->querybuf[0] 应该是*字符 */
        serverAssertWithInfo(c,NULL,c->querybuf[0] == '*');
        /*获取*字符后面紧跟的 长度字符串 转换成long long 类型 */
        /* 比如 *20\r\n */
        ok = string2ll(c->querybuf+1,newline-(c->querybuf+1),&ll);

        //如果长度没有 或者 长度大于1 mb
        if (!ok || ll > 1024*1024) {
            addReplyError(c,"Protocol error: invalid multibulk length");
            //内部会设置 CLIENT_CLOSE_AFTER_REPLY 标记 
            setProtocolError(c,pos);
            return C_ERR;
        }
        /*设置 位置到\r\n后*/
        pos = (newline-c->querybuf)+2;
        if (ll <= 0) {
            //如果长度小于等于0 直接返回ok?
            sdsrange(c->querybuf,pos,-1);
            return C_OK;
        }

        //https://www.cnblogs.com/macguz/p/15868573.html
        /**参数个数*/
        c->multibulklen = ll;

        /* Setup argv array on client structure */
        if (c->argv) zfree(c->argv);
        //为argv分配multibulklen 个 robj* 指针大小的内存空间 
        c->argv = zmalloc(sizeof(robj*)*c->multibulklen);
    }

    //multibulklen 必须大于0
    serverAssertWithInfo(c,NULL,c->multibulklen > 0);

    //根据multibulklen开始读取每个块
    while(c->multibulklen) {
        /* Read bulk length if unknown */
        /*读取 块长度 */
        if (c->bulklen == -1) {
            /*strchr函数功能为在一个串中查找给定字符的第一个匹配之处。
            函数原型为：char *strchr(const char *str, int c)，
            即在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置*/
            /*从 pos 开始查找 */
            newline = strchr(c->querybuf+pos,'\r');
            if (newline == NULL) {
                /*如果这时候 缓冲区大于64kb 也会返回 错误*/
                if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
                    addReplyError(c,
                        "Protocol error: too big bulk count string");
                    setProtocolError(c,0);
                    return C_ERR;
                }
                break;
            }
            
            //必须包含换行符
            /* Buffer should also contain \n */
            if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
                break;

            /*如果pos当前位置 的字符不是$ 那就是违反协议了*/
            if (c->querybuf[pos] != '$') {
                addReplyErrorFormat(c,
                    "Protocol error: expected '$', got '%c'",
                    c->querybuf[pos]);
                setProtocolError(c,pos);
                return C_ERR;
            }

            //长度的字符串转换为数字
            //+1 代表去掉$
            /*比如 *2\r\n$6\r\nSELECT\r\n$1\r\n0\r\n */
            //newline 为6字符开始 \r 第一次出现的位置
            ok = string2ll(c->querybuf+pos+1,newline-(c->querybuf+pos+1),&ll);
            //3.2还是有512mb的限制
            //https://ask.csdn.net/questions/7198670
            //https://github.com/redis/redis/issues/757
            //4.0版本已经改为 > proto_max_bulk_len 可以配置了
            if (!ok || ll < 0 || ll > 512*1024*1024) {
                addReplyError(c,"Protocol error: invalid bulk length");
                setProtocolError(c,pos);
                return C_ERR;
            }

            /*比如 *2\r\n$6\r\nSELECT\r\n$1\r\n0\r\n */
            /*这里pos就到达  SELECT 这里*/
            pos += newline-(c->querybuf+pos)+2;
            //PROTO_MBULK_BIG_ARG = 1024*32
            //当客户端传过来的长度大于等于1024*32 时
            if (ll >= PROTO_MBULK_BIG_ARG) {
                size_t qblen;

                /**
                 * 若我们要从网络中读取一个大型对象，请尝试使其可能从c->querybuf边界开始，
                 * 以便我们可以优化对象创建，避免大量数据拷贝。
                 * //也就是说 截取整个的querybuf 去跟传入的字符串长度对比
                 * //如果不满足长度 那么
                 * 
                 */
                /* If we are going to read a large object from network
                 * try to make it likely that it will start at c->querybuf
                 * boundary so that we can optimize object creation
                 * avoiding a large copy of data. */
                //c->querybuf 变为实际参数字符串
                /* 从pos截取整个字符串 */
                sdsrange(c->querybuf,pos,-1); //内部是根据sdslen来获取整个querybuf长度的
                pos = 0; //这里就等于0了 不然下面的判断 sdslen(c->querybuf)-pos  就会有问题
                qblen = sdslen(c->querybuf); //获取当前整个字符串的长度
                /* Hint the sds library about the amount of bytes this string is
                 * going to contain. */
                /*
                提示sds库此字符串将包含的字节数
                也就是分配空间

                如果当前查询缓冲区的长度 小于 需要的长度ll+2(\r\n)
                那么扩展空间
                */
                if (qblen < (size_t)ll+2)
                    c->querybuf = sdsMakeRoomFor(c->querybuf,ll+2-qblen); //提前再增加 ll+2-qblen 的空间 但是长度是不变的 只是分配的长度变了
            }
            c->bulklen = ll;
        }
        
        /* Read bulk argument */
        if (sdslen(c->querybuf)-pos < (unsigned)(c->bulklen+2)) {
            /** 实际的字符串长度 小于参数指明的长度  */
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            /* 优化： 如果缓冲 正好包含我们的bulk元素
                那就用当前这个sds
                而不是通过拷贝sds来创建一个新的对象 
             */
            /* Optimization: if the buffer contains JUST our bulk element
             * instead of creating a new object by *copying* the sds we
             * just use the current sds string. */
            if (pos == 0 &&
                c->bulklen >= PROTO_MBULK_BIG_ARG &&
                (signed) sdslen(c->querybuf) == c->bulklen+2)
            {
                //创建robj对象
                c->argv[c->argc++] = createObject(OBJ_STRING,c->querybuf);
                sdsIncrLen(c->querybuf,-2); /* remove CRLF */
                /* Assume that if we saw a fat argument we'll see another one
                 * likely... */
                /*假设如果我们看到一个胖的参数，我们可能会看到另一个*/
                /*所以 这边再给querybuf 分配c->bulklen+2 的长度*/
                c->querybuf = sdsnewlen(NULL,c->bulklen+2);
                sdsclear(c->querybuf);//把len 设置为0
                pos = 0;
            } else {
                //保存到c->argv里
                //createStringObject会根据 字符串长度 来 编码成EMBSTR 或者rawstring
                //内部就是拷贝字符串
                c->argv[c->argc++] =
                    createStringObject(c->querybuf+pos,c->bulklen);
                pos += c->bulklen+2; //位置增加
            }
            c->bulklen = -1;
            //参数数量减少
            c->multibulklen--;
        }
    }

    /* Trim to pos */
    /*从pos开始重新开始截取*/
    if (pos) sdsrange(c->querybuf,pos,-1);

    /* We're done when c->multibulk == 0 */
    if (c->multibulklen == 0) return C_OK;

    //仍然不能读取以处理命令
    /* Still not read to process the command */
    return C_ERR;
}

void processInputBuffer(client *c) {
    //到这里 才会设置当前客户端
    server.current_client = c;

    /*
    当输入缓冲区中有东西时继续处理
     判断长度 而不是判断指针
    */
    /* Keep processing while there is something in the input buffer */
    while(sdslen(c->querybuf)) {

        /*不是 slave的情况下 如果客户端处于暂停状态 那就直接跳出处理*/
        //也就是说 如果客户端是slave 那么 就不理会暂停状态

        //todo 需要知道客户端什么时候会处于暂停状态
        //客户端发送暂停指令 pauseClients方法 会使客户端暂停 
        //clientsArePaused 内部 如果暂停时间已到，那么会清空暂停标记
        /* Return if clients are paused. */
        if (!(c->flags & CLIENT_SLAVE) && clientsArePaused()) break;

        /*如果客户端正在处理某些事情，则立即中止*/
        /* Immediately abort if the client is in the middle of something. */
        if (c->flags & CLIENT_BLOCKED) break;

        /*
        CLIENT_CLOSE_AFTER_REPLY 在应答写入客户端后关闭连接
        确保在设置此标志后不要让reply增长（即不要处理更多命令）
        对于我们想要尽快终止的客户也是如此
        */
        /* CLIENT_CLOSE_AFTER_REPLY closes the connection once the reply is
         * written to the client. Make sure to not let the reply grow after
         * this flag has been set (i.e. don't process more commands).
         *
         * The same applies for clients we want to terminate ASAP. */
        if (c->flags & (CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP)) break;

        /*当不知道类型时检测请求类型*/
        /* Determine request type when unknown. */
        if (!c->reqtype) {
            //当第一个字符是* 时 就代表是MULTIBULK
            if (c->querybuf[0] == '*') {
                c->reqtype = PROTO_REQ_MULTIBULK;
            } else {
                //没有*号代表内联命令
                c->reqtype = PROTO_REQ_INLINE;
            }
        }

        if (c->reqtype == PROTO_REQ_INLINE) {
            //inline 
            if (processInlineBuffer(c) != C_OK) break;
        } else if (c->reqtype == PROTO_REQ_MULTIBULK) {
            if (processMultibulkBuffer(c) != C_OK) break;
        } else {
            serverPanic("Unknown request type");
        }

        //多个bulk 处理 有可能会看到 <=0 的长度
        /* Multibulk processing could see a <= 0 length. */
        if (c->argc == 0) {
            //让客户端准备处理下个命令
            resetClient(c);
        } else {
            /*开始处理命令*/
            /* Only reset the client when the command was executed. */
            if (processCommand(c) == C_OK)
                resetClient(c); //内部会释放客户端对象列表

            /*
              freeMemoryIfNeeded 方法 有可能刷新slave 输出缓冲区，
              这个可能导致一个从服务(有可能是当前活跃客户端） 被释放
            */
            /* freeMemoryIfNeeded may flush slave output buffers. This may result
             * into a slave, that may be the active client, to be freed. */
            if (server.current_client == NULL) break;
        }
    }
    server.current_client = NULL;
}
/*
 createClient 创建客户端的时候 会创建这个读事件
*/
//从客户端读取查询
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    client *c = (client*) privdata;
    int nread, readlen;
    size_t qblen;
    UNUSED(el);
    UNUSED(mask);

    readlen = PROTO_IOBUF_LEN; //普通I/O 缓冲区大小 16kb
    /*
    如果这是一个多批量请求，并且我们正在处理一个足够大的批量回复
    尽量使查询缓冲区恰好包含表示对象的SDS字符串的概率最大化
    即使冒着需要更多read(2)调用的风险
    这样processMultiBulkBuffer()函数可以避免复制缓冲区来创建代表参数的Redis对象
    */
    /* If this is a multi bulk request, and we are processing a bulk reply
     * that is large enough, try to maximize the probability that the query
     * buffer contains exactly the SDS string representing the object, even
     * at the risk of requiring more read(2) calls. This way the function
     * processMultiBulkBuffer() can avoid copying buffers to create the
     * Redis Object representing the argument. */

    /* PROTO_REQ_MULTIBULK 是在有*号的时候 */
    //当请求类型是multibulk 且multibulklen 大于0  且 bulklen 不等于-1
    //且 bulklen 大于 32 kb的时候
    //这里的判断是因为client有剩余还没处理的字符时
    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG)
    {
         //bulklen加上/r/n后 减去当前查询缓冲区的长度
         //计算下剩余bulk的长度还有多少
        int remaining = (unsigned)(c->bulklen+2)-sdslen(c->querybuf);

        //就是还需要读取多少长度就够一个bulk
        //如果剩余长度 小于准备读取的长度 那么读取长度 就变为 剩余长度
        if (remaining < readlen) readlen = remaining;
    }

    /*
        设置querybuf_peak 峰值
    */
    //先计算当前查询缓冲区的长度 c-querybuf 在创建client的时候initLen默认是0
    qblen = sdslen(c->querybuf);
    //如果最近查询缓冲区峰值 小于  查询缓冲区大小 那么 峰值就变为当前查询缓冲区大小
    //querybuf_peak 跟踪客户端输入缓冲区 (querybuf) 在生命周期内达到的最大容量，帮助识别异常或过大的命令请求
    if (c->querybuf_peak < qblen) c->querybuf_peak = qblen;

    //扩大字符数组以容纳读取的readlen长度
    //如果查询缓冲区 有足够空间 会立马返回
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);


    /*调用read函数 读取 字符到querybuf中 */
    /* c->querybuf+qblen 代表 移动到 c->querybuf+qblen 这个位置 */
    /*因为上面已经扩充空间了*/
    nread = read(fd, c->querybuf+qblen, readlen);

    //读完后为啥不更新c->querybuf_peak？

    if (nread == -1) {
        //errno == EAGAIN是Unix/Linux系统中表示‌资源暂时不可用‌的错误码
        //系统调用因资源不足被暂时拒绝，但后续重试可能成功
        if (errno == EAGAIN) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s",strerror(errno));
            //释放客户端
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        //telnet 关闭客户端时 会提示这个
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClient(c);
        //读取为0时直接返回了
        return;
    }
    /*长度增加 读取到的长度*/
    sdsIncrLen(c->querybuf,nread);
    //最后活跃时间更新
    c->lastinteraction = server.unixtime; //unixtime 会在调用updateCachedTime时更新
    //todo 如果当前client是master 那么偏移量增加 主从相关
    if (c->flags & CLIENT_MASTER) c->reploff += nread;
    //增加网络字节数
    server.stat_net_input_bytes += nread;

    //当前查询缓冲区长度  如果大于 最大缓冲区限制 那就释放客户端
    //client_max_querybuf_len 在initServerConfig的时候 会初始化为 1GB max query buffer

    //什么时候会大于1GB呢
    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {

        //ci就是可读的客户端信息字符串
        sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();

        bytes = sdscatrepr(bytes,c->querybuf,64);
        serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
        //释放
        sdsfree(ci);
        sdsfree(bytes);

        freeClient(c);
        return;
    }
    //开始处理输入
    //内部跳出 处理的话 其实这是c->querybuf 也是有值的
    processInputBuffer(c);
}

void getClientsMaxBuffers(unsigned long *longest_output_list,
                          unsigned long *biggest_input_buffer) {
    client *c;
    listNode *ln;
    listIter li;
    unsigned long lol = 0, bib = 0;

    listRewind(server.clients,&li);
    while ((ln = listNext(&li)) != NULL) {
        c = listNodeValue(ln);

        if (listLength(c->reply) > lol) lol = listLength(c->reply);
        if (sdslen(c->querybuf) > bib) bib = sdslen(c->querybuf);
    }
    *longest_output_list = lol;
    *biggest_input_buffer = bib;
}

/* A Redis "Peer ID" is a colon separated ip:port pair.
 * For IPv4 it's in the form x.y.z.k:port, example: "127.0.0.1:1234".
 * For IPv6 addresses we use [] around the IP part, like in "[::1]:1234".
 * For Unix sockets we use path:0, like in "/tmp/redis:0".
 *
 * A Peer ID always fits inside a buffer of NET_PEER_ID_LEN bytes, including
 * the null term.
 *
 * On failure the function still populates 'peerid' with the "?:0" string
 * in case you want to relax error checking or need to display something
 * anyway (see anetPeerToString implementation for more info). */
void genClientPeerId(client *client, char *peerid,
                            size_t peerid_len) {
    if (client->flags & CLIENT_UNIX_SOCKET) {
        /* Unix socket client. */
        snprintf(peerid,peerid_len,"%s:0",server.unixsocket);
    } else {
        /* TCP client. */
        anetFormatPeer(client->fd,peerid,peerid_len);
    }
}

/* This function returns the client peer id, by creating and caching it
 * if client->peerid is NULL, otherwise returning the cached value.
 * The Peer ID never changes during the life of the client, however it
 * is expensive to compute. */
char *getClientPeerId(client *c) {
    char peerid[NET_PEER_ID_LEN];

    if (c->peerid == NULL) {
        genClientPeerId(c,peerid,sizeof(peerid));
        c->peerid = sdsnew(peerid);
    }
    return c->peerid;
}

/*
以人类可读的格式连接一个表示客户端状态的字符串
*/
/* Concatenate a string representing the state of a client in an human
 * readable format, into the sds string 's'. */
sds catClientInfoString(sds s, client *client) {
    char flags[16], events[3], *p;
    int emask;

    p = flags;
    //如果是slave
    if (client->flags & CLIENT_SLAVE) {
        if (client->flags & CLIENT_MONITOR)
            *p++ = 'O';
        else
            *p++ = 'S';
    }
    if (client->flags & CLIENT_MASTER) *p++ = 'M'; //master
    if (client->flags & CLIENT_MULTI) *p++ = 'x'; //multi模式
    if (client->flags & CLIENT_BLOCKED) *p++ = 'b'; //blocked
    if (client->flags & CLIENT_DIRTY_CAS) *p++ = 'd'; //Watched
    if (client->flags & CLIENT_CLOSE_AFTER_REPLY) *p++ = 'c';
    if (client->flags & CLIENT_UNBLOCKED) *p++ = 'u';
    if (client->flags & CLIENT_CLOSE_ASAP) *p++ = 'A';
    if (client->flags & CLIENT_UNIX_SOCKET) *p++ = 'U';
    if (client->flags & CLIENT_READONLY) *p++ = 'r'; //只读
    if (p == flags) *p++ = 'N';
    *p++ = '\0'; //这个时候是指向的flags 

    emask = client->fd == -1 ? 0 : aeGetFileEvents(server.el,client->fd);
    p = events; //这个时候是指向的events数组
    if (emask & AE_READABLE) *p++ = 'r';
    if (emask & AE_WRITABLE) *p++ = 'w';
    *p = '\0';
    return sdscatfmt(s,
        "id=%U addr=%s fd=%i name=%s age=%I idle=%I flags=%s db=%i sub=%i psub=%i multi=%i qbuf=%U qbuf-free=%U obl=%U oll=%U omem=%U events=%s cmd=%s",
        (unsigned long long) client->id,//客户端id
        getClientPeerId(client),
        client->fd,
        client->name ? (char*)client->name->ptr : "",
        (long long)(server.unixtime - client->ctime),
        (long long)(server.unixtime - client->lastinteraction),
        flags,
        client->db->id,
        (int) dictSize(client->pubsub_channels),
        (int) listLength(client->pubsub_patterns),
        (client->flags & CLIENT_MULTI) ? client->mstate.count : -1,
        (unsigned long long) sdslen(client->querybuf),
        (unsigned long long) sdsavail(client->querybuf),
        (unsigned long long) client->bufpos,
        (unsigned long long) listLength(client->reply),
        (unsigned long long) getClientOutputBufferMemoryUsage(client),
        events,
        client->lastcmd ? client->lastcmd->name : "NULL");
}

sds getAllClientsInfoString(void) {
    listNode *ln;
    listIter li;
    client *client;
    sds o = sdsnewlen(NULL,200*listLength(server.clients));
    sdsclear(o);
    listRewind(server.clients,&li);
    while ((ln = listNext(&li)) != NULL) {
        client = listNodeValue(ln);
        o = catClientInfoString(o,client);
        o = sdscatlen(o,"\n",1);
    }
    return o;
}

void clientCommand(client *c) {
    listNode *ln;
    listIter li;
    client *client;

    if (!strcasecmp(c->argv[1]->ptr,"list") && c->argc == 2) {
        /* CLIENT LIST */
        sds o = getAllClientsInfoString();
        addReplyBulkCBuffer(c,o,sdslen(o));
        sdsfree(o);
    } else if (!strcasecmp(c->argv[1]->ptr,"reply") && c->argc == 3) {
        /* CLIENT REPLY ON|OFF|SKIP */
        if (!strcasecmp(c->argv[2]->ptr,"on")) {
            c->flags &= ~(CLIENT_REPLY_SKIP|CLIENT_REPLY_OFF);
            addReply(c,shared.ok);
        } else if (!strcasecmp(c->argv[2]->ptr,"off")) {
            c->flags |= CLIENT_REPLY_OFF;
        } else if (!strcasecmp(c->argv[2]->ptr,"skip")) {

            //如果当前不是OFF 就设置为CLIENT_REPLY_SKIP_NEXT
            if (!(c->flags & CLIENT_REPLY_OFF))
                c->flags |= CLIENT_REPLY_SKIP_NEXT;
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    } else if (!strcasecmp(c->argv[1]->ptr,"kill")) {
        /* CLIENT KILL <ip:port>
         * CLIENT KILL <option> [value] ... <option> [value] */
        char *addr = NULL;
        int type = -1;
        uint64_t id = 0;
        int skipme = 1;
        int killed = 0, close_this_client = 0;

        if (c->argc == 3) {
            /* Old style syntax: CLIENT KILL <addr> */
            addr = c->argv[2]->ptr;
            skipme = 0; /* With the old form, you can kill yourself. */
        } else if (c->argc > 3) {
            int i = 2; /* Next option index. */

            /* New style syntax: parse options. */
            while(i < c->argc) {
                int moreargs = c->argc > i+1;

                if (!strcasecmp(c->argv[i]->ptr,"id") && moreargs) {
                    long long tmp;

                    if (getLongLongFromObjectOrReply(c,c->argv[i+1],&tmp,NULL)
                        != C_OK) return;
                    id = tmp;
                } else if (!strcasecmp(c->argv[i]->ptr,"type") && moreargs) {
                    type = getClientTypeByName(c->argv[i+1]->ptr);
                    if (type == -1) {
                        addReplyErrorFormat(c,"Unknown client type '%s'",
                            (char*) c->argv[i+1]->ptr);
                        return;
                    }
                } else if (!strcasecmp(c->argv[i]->ptr,"addr") && moreargs) {
                    addr = c->argv[i+1]->ptr;
                } else if (!strcasecmp(c->argv[i]->ptr,"skipme") && moreargs) {
                    if (!strcasecmp(c->argv[i+1]->ptr,"yes")) {
                        skipme = 1;
                    } else if (!strcasecmp(c->argv[i+1]->ptr,"no")) {
                        skipme = 0;
                    } else {
                        addReply(c,shared.syntaxerr);
                        return;
                    }
                } else {
                    addReply(c,shared.syntaxerr);
                    return;
                }
                i += 2;
            }
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }

        /* Iterate clients killing all the matching clients. */
        listRewind(server.clients,&li);
        while ((ln = listNext(&li)) != NULL) {
            client = listNodeValue(ln);
            if (addr && strcmp(getClientPeerId(client),addr) != 0) continue;
            if (type != -1 && getClientType(client) != type) continue;
            if (id != 0 && client->id != id) continue;
            if (c == client && skipme) continue;

            /* Kill it. */
            if (c == client) {
                close_this_client = 1;
            } else {
                freeClient(client);
            }
            killed++;
        }

        /* Reply according to old/new format. */
        if (c->argc == 3) {
            if (killed == 0)
                addReplyError(c,"No such client");
            else
                addReply(c,shared.ok);
        } else {
            addReplyLongLong(c,killed);
        }

        /*
            如果这个客户端必须关闭，只有在我们将应答排队到它的输出缓冲区之后，才将它标记为CLOSE_AFTER_REPLY

            因为这个命令 有可能是另外一个客户端设置的
        */
        /* If this client has to be closed, flag it as CLOSE_AFTER_REPLY
         * only after we queued the reply to its output buffers. */
        if (close_this_client) c->flags |= CLIENT_CLOSE_AFTER_REPLY;
    } else if (!strcasecmp(c->argv[1]->ptr,"setname") && c->argc == 3) {
        int j, len = sdslen(c->argv[2]->ptr);
        char *p = c->argv[2]->ptr;

        /* Setting the client name to an empty string actually removes
         * the current name. */
        if (len == 0) {
            if (c->name) decrRefCount(c->name);
            c->name = NULL;
            addReply(c,shared.ok);
            return;
        }

        /* Otherwise check if the charset is ok. We need to do this otherwise
         * CLIENT LIST format will break. You should always be able to
         * split by space to get the different fields. */
        for (j = 0; j < len; j++) {
            if (p[j] < '!' || p[j] > '~') { /* ASCII is assumed. */
                addReplyError(c,
                    "Client names cannot contain spaces, "
                    "newlines or special characters.");
                return;
            }
        }
        if (c->name) decrRefCount(c->name);
        c->name = c->argv[2];
        incrRefCount(c->name);
        addReply(c,shared.ok);
    } else if (!strcasecmp(c->argv[1]->ptr,"getname") && c->argc == 2) {
        if (c->name)
            addReplyBulk(c,c->name);
        else
            addReply(c,shared.nullbulk);
    } else if (!strcasecmp(c->argv[1]->ptr,"pause") && c->argc == 3) {
        long long duration;

        if (getTimeoutFromObjectOrReply(c,c->argv[2],&duration,UNIT_MILLISECONDS)
                                        != C_OK) return;
        
        //‌Redis Client Pause命令用于在指定时间内终止运行来自客户端的命令‌。
        //该命令可以阻塞客户端一段时间，通常用于调试或测试场景中，以避免客户端在执行某些操作时对Redis服务器造成过载。
        pauseClients(duration); //CLIENT PAUSE	挂起客户端连接，将所有客户端挂起指定的时间（以毫秒为计算）
        addReply(c,shared.ok);
    } else {
        addReplyError(c, "Syntax error, try CLIENT (LIST | KILL | GETNAME | SETNAME | PAUSE | REPLY)");
    }
}

/* This callback is bound to POST and "Host:" command names. Those are not
 * really commands, but are used in security attacks in order to talk to
 * Redis instances via HTTP, with a technique called "cross protocol scripting"
 * which exploits the fact that services like Redis will discard invalid
 * HTTP headers and will process what follows.
 *
 * As a protection against this attack, Redis will terminate the connection
 * when a POST or "Host:" header is seen, and will log the event from
 * time to time (to avoid creating a DOS as a result of too many logs). */
void securityWarningCommand(client *c) {
    static time_t logged_time;
    time_t now = time(NULL);

    if (labs(now-logged_time) > 60) {
        serverLog(LL_WARNING,"Possible SECURITY ATTACK detected. It looks like somebody is sending POST or Host: commands to Redis. This is likely due to an attacker attempting to use Cross Protocol Scripting to compromise your Redis instance. Connection aborted.");
        logged_time = now;
    }
    freeClientAsync(c);
}

/* Rewrite the command vector of the client. All the new objects ref count
 * is incremented. The old command vector is freed, and the old objects
 * ref count is decremented. */
void rewriteClientCommandVector(client *c, int argc, ...) {
    va_list ap;
    int j;
    robj **argv; /* The new argument vector */

    argv = zmalloc(sizeof(robj*)*argc);
    va_start(ap,argc);
    for (j = 0; j < argc; j++) {
        robj *a;

        a = va_arg(ap, robj*);
        argv[j] = a;
        incrRefCount(a);
    }
    /* We free the objects in the original vector at the end, so we are
     * sure that if the same objects are reused in the new vector the
     * refcount gets incremented before it gets decremented. */
    for (j = 0; j < c->argc; j++) decrRefCount(c->argv[j]);
    zfree(c->argv);
    /* Replace argv and argc with our new versions. */
    c->argv = argv;
    c->argc = argc;
    c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
    serverAssertWithInfo(c,NULL,c->cmd != NULL);
    va_end(ap);
}

/* Completely replace the client command vector with the provided one. */
void replaceClientCommandVector(client *c, int argc, robj **argv) {
    freeClientArgv(c);
    zfree(c->argv);
    c->argv = argv;
    c->argc = argc;
    c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
    serverAssertWithInfo(c,NULL,c->cmd != NULL);
}

/* Rewrite a single item in the command vector.
 * The new val ref count is incremented, and the old decremented.
 *
 * It is possible to specify an argument over the current size of the
 * argument vector: in this case the array of objects gets reallocated
 * and c->argc set to the max value. However it's up to the caller to
 *
 * 1. Make sure there are no "holes" and all the arguments are set.
 * 2. If the original argument vector was longer than the one we
 *    want to end with, it's up to the caller to set c->argc and
 *    free the no longer used objects on c->argv. */
void rewriteClientCommandArgument(client *c, int i, robj *newval) {
    robj *oldval;

    if (i >= c->argc) {
        c->argv = zrealloc(c->argv,sizeof(robj*)*(i+1));
        c->argc = i+1;
        c->argv[i] = NULL;
    }
    oldval = c->argv[i];
    c->argv[i] = newval;
    incrRefCount(newval);
    if (oldval) decrRefCount(oldval);

    /* If this is the command name make sure to fix c->cmd. */
    if (i == 0) {
        c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
        serverAssertWithInfo(c,NULL,c->cmd != NULL);
    }
}

/*
 这个函数返回Redis实际上用来存储客户端仍然没有读取的回复的字节数，
 它是“虚拟的”，因为应答输出列表可能包含共享的对象，并且实际上没有使用额外的内存

*/
/* This function returns the number of bytes that Redis is virtually
 * using to store the reply still not read by the client.
 * It is "virtual" since the reply output list may contain objects that
 * are shared and are not really using additional memory.
 *
 * The function returns the total sum of the length of all the objects
 * stored in the output list, plus the memory used to allocate every
 * list node. The static reply buffer is not taken into account since it
 * is allocated anyway.
 *
 * Note: this function is very fast so can be called as many time as
 * the caller wishes. The main usage of this function currently is
 * enforcing the client output length limits. */
unsigned long getClientOutputBufferMemoryUsage(client *c) {

    //每个链表节点所占的空间
    unsigned long list_item_size = sizeof(listNode)+sizeof(robj);

    return c->reply_bytes + (list_item_size*listLength(c->reply));
}

/* Get the class of a client, used in order to enforce limits to different
 * classes of clients.
 *
 * The function will return one of the following:
 * CLIENT_TYPE_NORMAL -> Normal client
 * CLIENT_TYPE_SLAVE  -> Slave or client executing MONITOR command
 * CLIENT_TYPE_PUBSUB -> Client subscribed to Pub/Sub channels
 * CLIENT_TYPE_MASTER -> The client representing our replication master.
 */
int getClientType(client *c) {
    if (c->flags & CLIENT_MASTER) return CLIENT_TYPE_MASTER;
    if ((c->flags & CLIENT_SLAVE) && !(c->flags & CLIENT_MONITOR))
        return CLIENT_TYPE_SLAVE;
    if (c->flags & CLIENT_PUBSUB) return CLIENT_TYPE_PUBSUB;
    return CLIENT_TYPE_NORMAL;
}

int getClientTypeByName(char *name) {
    if (!strcasecmp(name,"normal")) return CLIENT_TYPE_NORMAL;
    else if (!strcasecmp(name,"slave")) return CLIENT_TYPE_SLAVE;
    else if (!strcasecmp(name,"pubsub")) return CLIENT_TYPE_PUBSUB;
    else if (!strcasecmp(name,"master")) return CLIENT_TYPE_MASTER;
    else return -1;
}

char *getClientTypeName(int class) {
    switch(class) {
    case CLIENT_TYPE_NORMAL: return "normal";
    case CLIENT_TYPE_SLAVE:  return "slave";
    case CLIENT_TYPE_PUBSUB: return "pubsub";
    case CLIENT_TYPE_MASTER: return "master";
    default:                       return NULL;
    }
}

/* The function checks if the client reached output buffer soft or hard
 * limit, and also update the state needed to check the soft limit as
 * a side effect.
 *
 * Return value: non-zero if the client reached the soft or the hard limit.
 *               Otherwise zero is returned. */
int checkClientOutputBufferLimits(client *c) {
    int soft = 0, hard = 0, class;
    unsigned long used_mem = getClientOutputBufferMemoryUsage(c);

    class = getClientType(c);
    /* For the purpose of output buffer limiting, masters are handled
     * like normal clients. */
    if (class == CLIENT_TYPE_MASTER) class = CLIENT_TYPE_NORMAL;

    if (server.client_obuf_limits[class].hard_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
        hard = 1;
    if (server.client_obuf_limits[class].soft_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
        soft = 1;

    /* We need to check if the soft limit is reached continuously for the
     * specified amount of seconds. */
    if (soft) {
        if (c->obuf_soft_limit_reached_time == 0) {
            c->obuf_soft_limit_reached_time = server.unixtime;
            soft = 0; /* First time we see the soft limit reached */
        } else {
            time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

            if (elapsed <=
                server.client_obuf_limits[class].soft_limit_seconds) {
                soft = 0; /* The client still did not reached the max number of
                             seconds for the soft limit to be considered
                             reached. */
            }
        }
    } else {
        c->obuf_soft_limit_reached_time = 0;
    }
    return soft || hard;
}

/* Asynchronously close a client if soft or hard limit is reached on the
 * output buffer size. The caller can check if the client will be closed
 * checking if the client CLIENT_CLOSE_ASAP flag is set.
 *
 * Note: we need to close the client asynchronously because this function is
 * called from contexts where the client can't be freed safely, i.e. from the
 * lower level functions pushing data inside the client output buffers. */
void asyncCloseClientOnOutputBufferLimitReached(client *c) {
    serverAssert(c->reply_bytes < SIZE_MAX-(1024*64));
    if (c->reply_bytes == 0 || c->flags & CLIENT_CLOSE_ASAP) return;
    if (checkClientOutputBufferLimits(c)) {
        sds client = catClientInfoString(sdsempty(),c);

        freeClientAsync(c);
        serverLog(LL_WARNING,"Client %s scheduled to be closed ASAP for overcoming of output buffer limits.", client);
        sdsfree(client);
    }
}

/**
 * 
 * freeMemoryIfNeeded（）使用的辅助函数，
 * 目的是在不将控制权返回给事件循环的情况下，
 * 清除slave的输出缓冲区。SHUTDOWN也会调用这个函数，表示尽最大努力向slave发送最新的写操作。
 */
/* Helper function used by freeMemoryIfNeeded() in order to flush slaves
 * output buffers without returning control to the event loop.
 * This is also called by SHUTDOWN for a best-effort attempt to send
 * slaves the latest writes. */
void flushSlavesOutputBuffers(void) {
    listIter li;
    listNode *ln;

    //把当前服务的所有从服务开始迭代
    listRewind(server.slaves,&li);
    while((ln = listNext(&li))) {
        client *slave = listNodeValue(ln);
        int events;

        /* Note that the following will not flush output buffers of slaves
         * in STATE_ONLINE but having put_online_on_ack set to true: in this
         * case the writable event is never installed, since the purpose
         * of put_online_on_ack is to postpone the moment it is installed.
         * This is what we want since slaves in this state should not receive
         * writes before the first ACK. */
        events = aeGetFileEvents(server.el,slave->fd);
        if (events & AE_WRITABLE &&
            slave->replstate == SLAVE_STATE_ONLINE &&
            clientHasPendingReplies(slave))
        {
            writeToClient(slave->fd,slave,0);
        }
    }
}

/*
将客户端暂停到指定的unix时间（以毫秒为单位）。
当客户机暂停时，不会处理来自客户机的命令，因此在此期间数据集不能更改

此功能暂停正常的和Pub/Sub客户端，但是仍然会为从端提供服务，
因此此功能可以用于服务器升级，其中需要 从机在转到主机之前处理复制流中的最新字节

*/
/* Pause clients up to the specified unixtime (in ms). While clients
 * are paused no command is processed from clients, so the data set can't
 * change during that time.
 *
 * However while this function pauses normal and Pub/Sub clients, slaves are
 * still served, so this function can be used on server upgrades where it is
 * required that slaves process the latest bytes from the replication stream
 * before being turned to masters.
 *
 * This function is also internally used by Redis Cluster for the manual
 * failover procedure implemented by CLUSTER FAILOVER.
 *
 * The function always succeed, even if there is already a pause in progress.
 * In such a case, the pause is extended if the duration is more than the
 * time left for the previous duration. However if the duration is smaller
 * than the time left for the previous pause, no change is made to the
 * left duration. */
void pauseClients(mstime_t end) {
    //如果当前没有暂停 或者 当前发送的终止时间大于当前设置的时间
    if (!server.clients_paused || end > server.clients_pause_end_time)
        server.clients_pause_end_time = end;
    server.clients_paused = 1;
}

/*
如果客户端当前暂停就返回非0
副作用是 该函数检查是否达到暂停时间并清除它
*/
/* Return non-zero if clients are currently paused. As a side effect the
 * function checks if the pause time was reached and clear it. */
int clientsArePaused(void) {
    //如果当前客户端被阻塞 且 阻塞截止时间 早于当前时间 
    //那么就复位
    //且把不是slave 且不是本身阻塞的 客户端加入到unblocked_clients 列表中

    //server.mstime 会在updateCachedTime 的时候更新
    if (server.clients_paused &&
        server.clients_pause_end_time < server.mstime)
    {
        //这个时候 暂停结束了
        listNode *ln;
        listIter li;
        client *c;

        //截止时间 小于当前时间 就清空暂停标记
        server.clients_paused = 0;

        /** 将所有客户端放入未阻止的客户端队列中，以便强制重新处理输入缓冲区（如果有的话） */
        /* Put all the clients in the unblocked clients queue in order to
         * force the re-processing of the input buffer if any. */
        listRewind(server.clients,&li);
        while ((ln = listNext(&li)) != NULL) {
            c = listNodeValue(ln);

            //不管slave和blocked 的客户端。
            //取消阻止后，将处理后一个挂起的请求。
            /* Don't touch slaves and blocked clients. The latter pending
             * requests be processed when unblocked. */
            if (c->flags & (CLIENT_SLAVE|CLIENT_BLOCKED)) continue;
            c->flags |= CLIENT_UNBLOCKED;

            //把不是slave 且不是本身阻塞的 客户端加入到unblocked_clients 列表中
            listAddNodeTail(server.unblocked_clients,c);
        }
    }
    return server.clients_paused;
}

/* This function is called by Redis in order to process a few events from
 * time to time while blocked into some not interruptible operation.
 * This allows to reply to clients with the -LOADING error while loading the
 * data set at startup or after a full resynchronization with the master
 * and so forth.
 *
 * It calls the event loop in order to process a few events. Specifically we
 * try to call the event loop 4 times as long as we receive acknowledge that
 * some event was processed, in order to go forward with the accept, read,
 * write, close sequence needed to serve a client.
 *
 * The function returns the total number of events processed. */
int processEventsWhileBlocked(void) {
    int iterations = 4; /* See the function top-comment. */
    int count = 0;
    while (iterations--) {
        int events = 0;
        events += aeProcessEvents(server.el, AE_FILE_EVENTS|AE_DONT_WAIT);
        events += handleClientsWithPendingWrites();
        if (!events) break;
        count += events;
    }
    return count;
}
