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
#include "cluster.h"

#include <signal.h>
#include <ctype.h>

void slotToKeyAdd(robj *key);
void slotToKeyDel(robj *key);
void slotToKeyFlush(void);

/*-----------------------------------------------------------------------------
 * C-level DB API
 *----------------------------------------------------------------------------*/

/* Low level key lookup API, not actually called directly from commands
 * implementations that should instead rely on lookupKeyRead(),
 * lookupKeyWrite() and lookupKeyReadWithFlags(). */
robj *lookupKey(redisDb *db, robj *key, int flags) {
    /*key->ptr代表实际值*/
    dictEntry *de = dictFind(db->dict,key->ptr);
    if (de) {
        robj *val = dictGetVal(de);

        /*https://mp.weixin.qq.com/s?__biz=MzU0OTE4MzYzMw==&mid=2247560043&idx=2&sn=48494925dbd6f19712c346156c4c3559&chksm=fbb06295ccc7eb838638af3646d6aa9918d0fcd5f4a0c95bacb71380f6a3cbe2101596250ba2&scene=27*/
        /*更新老化算法的访问时间*/
        /*如果我们有一个保存子节点(rdb,aof)，不要这样做，因为这会触发写时复制的疯狂行为*/
        /* Update the access time for the ageing algorithm.
         * Don't do it if we have a saving child, as this will trigger
         * a copy on write madness. */
        if (server.rdb_child_pid == -1 &&
            server.aof_child_pid == -1 &&
            !(flags & LOOKUP_NOTOUCH))
        {
            /*LRU算法广泛应用在诸多系统内，例如Linux内核页表交换，MySQL Buffer Pool缓存页替换，以及Redis数据淘汰策略*/
            /*The Least Recently Used*/
            /*成员变量lru字段用于记录了此key最近一次被访问的LRU时钟(server.lruclock)*/
            val->lru = LRU_CLOCK();
        }
        return val;
    } else {
        return NULL;
    }
}

/* Lookup a key for read operations, or return NULL if the key is not found
 * in the specified DB.
 *
 * As a side effect(副作用) of calling this function:
 * 1. A key gets expired if it reached it's TTL. //一个键过期 如果达到它的TTL(Time To Live)
 * 2. The key last access time is updated. //key的最后访问时间会更新
 * 3. The global keys hits/misses stats are updated (reported in INFO). //全局key hit/miss 命中和未命中会更新
 *
 * 当我们在获得链接到键的对象后写入键时，不应该使用这个API，而应该只用于只读操作
 * This API should not be used when we write to the key after obtaining
 * the object linked to the key, but only for read only operations.
 *
 * Flags change the behavior of this command:
 *
 *  LOOKUP_NONE (or zero): no special flags are passed.
 *  LOOKUP_NOTOUCH: don't alter the last access time of the key.
 *
 *  如果键在逻辑上已经过期，这个函数也会返回NULL但仍然存在，以防这是一个slave，因为这个API只被调用表示读操作。即使密钥过期是master-driven的，我们也可以
 *  正确地报告一个key在slave上过期，即使master是滞后的
 *  通过复制链接中的DELs使键过期
 * Note: this function also returns NULL is the key is logically expired
 * but still existing, in case this is a slave, since this API is called only
 * for read operations. Even if the key expiry is master-driven, we can
 * correctly report a key is expired on slaves even if the master is lagging
 * expiring our key via DELs in the replication link. */
robj *lookupKeyReadWithFlags(redisDb *db, robj *key, int flags) {
    robj *val;

    /*当expireIfNeeded返回1的时候代表过期了 */
    if (expireIfNeeded(db,key) == 1) {

        /* 键过期了，如果我们在master的上下文中，expireIfNeeded 只会在key不存在的时候返回0 */
        /* Key expired. If we are in the context of a master, expireIfNeeded()
         * returns 0 only when the key does not exist at all, so it's safe
         * to return NULL ASAP. */
        if (server.masterhost == NULL) return NULL;

        /*
        然而，如果我们在一个slave的上下文中，expireIfNeeded()将会
        不是真的尝试过期密钥，它只返回关于密钥的“逻辑”状态信息:
        键过期取决于主服务器，以便对主服务器的数据集有一个一致的视图

        但是，如果命令调用者不是master，并且作为额外的安全措施，所调用的命令是只读命令，
        那么我们可以在这里安全地返回NULL，并且以只读方式访问过期值的客户端提供更一致的行为，这将表明键不存在

        值得注意的是，当从服务器用于扩展读操作时，这包括get操作
        */
        /* However if we are in the context of a slave, expireIfNeeded() will
         * not really try to expire the key, it only returns information
         * about the "logical" status of the key: key expiring is up to the
         * master in order to have a consistent view of master's data set.
         *
         * However, if the command caller is not the master, and as additional
         * safety measure, the command invoked is a read-only command, we can
         * safely return NULL here, and provide a more consistent behavior
         * to clients accessign expired values in a read-only fashion, that
         * will say the key as non exisitng.
         *
         * Notably this covers GETs when slaves are used to scale reads. */
        if (server.current_client &&
            server.current_client != server.master &&
            server.current_client->cmd &&
            server.current_client->cmd->flags & CMD_READONLY)
        {
            //命令的标记 是通过命令列表里的flag指定的
            //如果当前 客户端不是我们的主服务器 且命令的标记为readonly 那就返回空
            return NULL;
        }
    }
    val = lookupKey(db,key,flags);
    if (val == NULL)
        server.stat_keyspace_misses++;
    else
        server.stat_keyspace_hits++;
    return val;
}

/* Like lookupKeyReadWithFlags(), but does not use any flag, which is the
 * common case. */
robj *lookupKeyRead(redisDb *db, robj *key) {
    return lookupKeyReadWithFlags(db,key,LOOKUP_NONE);
}

/* Lookup a key for write operations, and as a side effect, if needed, expires
 * the key if its TTL is reached.
 *
 * Returns the linked value object if the key exists or NULL if the key
 * does not exist in the specified DB. */
robj *lookupKeyWrite(redisDb *db, robj *key) {
    expireIfNeeded(db,key);
    return lookupKey(db,key,LOOKUP_NONE);
}

robj *lookupKeyReadOrReply(client *c, robj *key, robj *reply) {
    robj *o = lookupKeyRead(c->db, key);
    if (!o) addReply(c,reply);
    return o;
}

robj *lookupKeyWriteOrReply(client *c, robj *key, robj *reply) {
    robj *o = lookupKeyWrite(c->db, key);
    if (!o) addReply(c,reply);
    return o;
}

/* Add the key to the DB. It's up to the caller to increment the reference
 * counter of the value if needed.
 *
 * The program is aborted if the key already exists. */
void dbAdd(redisDb *db, robj *key, robj *val) {

    /* 这里如果是小于32字节的字符串的时候 会编码为sdshdr5 类型的  */
    //这里会啥要复制一个字符串呢 我的理解是因为在 freeClientArgv 的时候 因为key这个对象的引用计数为1 所以会释放
    //但是 这边的key字符串不能释放，所以要复制下
    sds copy = sdsdup(key->ptr); 
    int retval = dictAdd(db->dict, copy, val);

    serverAssertWithInfo(NULL,key,retval == DICT_OK);
    /*值类型为list*/
    if (val->type == OBJ_LIST) signalListAsReady(db, key);

    /*集群模式*/
    /*计算出key对应所属的槽位*/
    if (server.cluster_enabled) slotToKeyAdd(key);
 }

/* Overwrite an existing key with a new value. Incrementing the reference
 * count of the new value is up to the caller.
 * This function does not modify the expire time of the existing key.
 *
 * The program is aborted if the key was not already present. */
void dbOverwrite(redisDb *db, robj *key, robj *val) {
    dictEntry *de = dictFind(db->dict,key->ptr);

    serverAssertWithInfo(NULL,key,de != NULL);
    dictReplace(db->dict, key->ptr, val);
}

/* High level Set operation. This function can be used in order to set
 * a key, whatever it was existing or not, to a new object.
 *
 * 1) The ref count of the value object is incremented.
 * 2) clients WATCHing for the destination key notified.
 * 3) The expire time of the key is reset (the key is made persistent). */
void setKey(redisDb *db, robj *key, robj *val) {
    if (lookupKeyWrite(db,key) == NULL) {
        dbAdd(db,key,val);
    } else {
        dbOverwrite(db,key,val);
    }
    incrRefCount(val); //增加value对象的计数 这样释放客户端参数的时候会不会释放了 freeClientArgv
    removeExpire(db,key);

    //发送key被修改的信号
    signalModifiedKey(db,key);
}

int dbExists(redisDb *db, robj *key) {
    return dictFind(db->dict,key->ptr) != NULL;
}

/* Return a random key, in form of a Redis object.
 * If there are no keys, NULL is returned.
 *
 * The function makes sure to return keys not already expired. */
robj *dbRandomKey(redisDb *db) {
    dictEntry *de;

    while(1) {
        sds key;
        robj *keyobj;

        de = dictGetRandomKey(db->dict);
        if (de == NULL) return NULL;

        key = dictGetKey(de);
        keyobj = createStringObject(key,sdslen(key));
        if (dictFind(db->expires,key)) {
            if (expireIfNeeded(db,keyobj)) {
                decrRefCount(keyobj);
                continue; /* search for another key. This expired. */
            }
        }
        return keyobj;
    }
}

/* Delete a key, value, and associated expiration entry if any, from the DB */
int dbDelete(redisDb *db, robj *key) {
    /* Deleting an entry from the expires dict will not free the sds of
     * the key, because it is shared with the main dictionary. */

    /*因为 expires 字典是 keyptrDictType 这个类型的字典的key和value的 析构函数为空，所以不会释放key */
    if (dictSize(db->expires) > 0) dictDelete(db->expires,key->ptr);
    if (dictDelete(db->dict,key->ptr) == DICT_OK) {
        if (server.cluster_enabled) slotToKeyDel(key);
        return 1;
    } else {
        return 0;
    }
}

/*
准备存储在‘key’处的字符串对象，以便对其进行破坏性修改，以实现SETBIT或APPEND等命令
对象通常准备好修改，除非两个条件中的一个为真

1. 对象‘o’是共享的（refcount > 1），我们不希望影响其他用户
2. 对象编码不是raw

如果该函数在上述条件之一（或两者）中找到该对象，
则该字符串对象的非共享/未编码副本将存储在指定的‘db’中的‘key’处。
否则将返回对象‘o’本身

用法：
对象‘o’是调用者已经通过在‘db’中查找‘key’获得的，使用模式看起来像这样
o = lookupKeyWrite(db,key);
if (checkType(c,o,OBJ_STRING)) return;
o = dbUnshareStringValue(db,key,o);
此时，调用者已经准备好修改对象，例如使用sdscat（）调用来附加一些数据或其他任何内容
*/
/* Prepare the string object stored at 'key' to be modified destructively
 * to implement commands like SETBIT or APPEND.
 *
 * An object is usually ready to be modified unless one of the two conditions
 * are true:
 *
 * 1) The object 'o' is shared (refcount > 1), we don't want to affect
 *    other users.
 * 2) The object encoding is not "RAW".
 *
 * If the object is found in one of the above conditions (or both) by the
 * function, an unshared / not-encoded copy of the string object is stored
 * at 'key' in the specified 'db'. Otherwise the object 'o' itself is
 * returned.
 *
 * USAGE:
 *
 * The object 'o' is what the caller already obtained by looking up 'key'
 * in 'db', the usage pattern looks like this:
 *
 * o = lookupKeyWrite(db,key);
 * if (checkType(c,o,OBJ_STRING)) return;
 * o = dbUnshareStringValue(db,key,o);
 *
 * At this point the caller is ready to modify the object, for example
 * using an sdscat() call to append some data, or anything else.
 */
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o) {
    serverAssert(o->type == OBJ_STRING);
    //如果对象引用不只一个 或者编码不是raw
    if (o->refcount != 1 || o->encoding != OBJ_ENCODING_RAW) {
        //先解码
        robj *decoded = getDecodedObject(o);
        //创建raw类型的对象
        o = createRawStringObject(decoded->ptr, sdslen(decoded->ptr));
        //降低原来的引用计数
        decrRefCount(decoded);
        //覆盖key处的值
        dbOverwrite(db,key,o);
    }
    return o;
}

long long emptyDb(void(callback)(void*)) {
    int j;
    long long removed = 0;

    for (j = 0; j < server.dbnum; j++) {
        removed += dictSize(server.db[j].dict);
        dictEmpty(server.db[j].dict,callback);
        dictEmpty(server.db[j].expires,callback);
    }
    if (server.cluster_enabled) slotToKeyFlush();
    return removed;
}

int selectDb(client *c, int id) {
    if (id < 0 || id >= server.dbnum)
        return C_ERR;
    c->db = &server.db[id];
    return C_OK;
}


/*
包含通知WATCH列表、通知客户端更新缓存

 每次键被修改的时候 signalModifiedKey方法就会被调用

 每次数据库被flush signalFlushDb 方法会被调用
*/
/*-----------------------------------------------------------------------------
 * Hooks for key space changes.
 *
 * Every time a key in the database is modified the function
 * signalModifiedKey() is called.
 *
 * Every time a DB is flushed the function signalFlushDb() is called.
 *----------------------------------------------------------------------------*/

void signalModifiedKey(redisDb *db, robj *key) {
    touchWatchedKey(db,key);
}

void signalFlushedDb(int dbid) {
    touchWatchedKeysOnFlush(dbid);
}

/*-----------------------------------------------------------------------------
 * Type agnostic commands operating on the key space
 *----------------------------------------------------------------------------*/

void flushdbCommand(client *c) {
    server.dirty += dictSize(c->db->dict);
    signalFlushedDb(c->db->id);
    dictEmpty(c->db->dict,NULL);
    dictEmpty(c->db->expires,NULL);
    if (server.cluster_enabled) slotToKeyFlush();
    addReply(c,shared.ok);
}

void flushallCommand(client *c) {
    signalFlushedDb(-1);
    server.dirty += emptyDb(NULL);
    addReply(c,shared.ok);
    if (server.rdb_child_pid != -1) {
        kill(server.rdb_child_pid,SIGUSR1);
        rdbRemoveTempFile(server.rdb_child_pid);
    }
    if (server.saveparamslen > 0) {
        /* Normally rdbSave() will reset dirty, but we don't want this here
         * as otherwise FLUSHALL will not be replicated nor put into the AOF. */
        int saved_dirty = server.dirty;
        rdbSave(server.rdb_filename);
        server.dirty = saved_dirty;
    }
    server.dirty++;
}

void delCommand(client *c) {
    int deleted = 0, j;

    for (j = 1; j < c->argc; j++) {
        expireIfNeeded(c->db,c->argv[j]);
        //内部也会删除expire字典中的对应key value 
        if (dbDelete(c->db,c->argv[j])) {
            signalModifiedKey(c->db,c->argv[j]);
            notifyKeyspaceEvent(NOTIFY_GENERIC,
                "del",c->argv[j],c->db->id);
            server.dirty++;
            deleted++;
        }
    }
    addReplyLongLong(c,deleted);
}

/* EXISTS key1 key2 ... key_N.
 * Return value is the number of keys existing. */
/*

 */
void existsCommand(client *c) {
    long long count = 0;
    int j;

    for (j = 1; j < c->argc; j++) {
        /*3.2并未解决exists这个命令 从库获取键过期的问题*/
        /*4.0.14 中 直接调用 封装好的lookupKeyRead */
        expireIfNeeded(c->db,c->argv[j]);
        if (dbExists(c->db,c->argv[j])) count++;
    }
    addReplyLongLong(c,count);
}

void selectCommand(client *c) {
    long id;

    //参数中获取id
    if (getLongFromObjectOrReply(c, c->argv[1], &id,
        "invalid DB index") != C_OK)
        return;

    /*
        哦这里判断了
        另外 SELECT 命令在集群中，除了 SELECT 0 外都是不允许的，因为 redis cluster 将所有键都保存在 0 号数据库中，不支持像单实例那样的多数据库
    */
    if (server.cluster_enabled && id != 0) {
        addReplyError(c,"SELECT is not allowed in cluster mode");
        return;
    }
    if (selectDb(c,id) == C_ERR) {
        addReplyError(c,"invalid DB index");
    } else {
        addReply(c,shared.ok);
    }
}

void randomkeyCommand(client *c) {
    robj *key;

    if ((key = dbRandomKey(c->db)) == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    addReplyBulk(c,key);
    decrRefCount(key);
}

void keysCommand(client *c) {
    dictIterator *di;
    dictEntry *de;
    sds pattern = c->argv[1]->ptr;
    int plen = sdslen(pattern), allkeys;
    unsigned long numkeys = 0;

    //生成一个list tail 节点 并且 返回这个节点
    void *replylen = addDeferredMultiBulkLength(c);

    di = dictGetSafeIterator(c->db->dict);
    allkeys = (pattern[0] == '*' && pattern[1] == '\0');
    while((de = dictNext(di)) != NULL) {
        sds key = dictGetKey(de);
        robj *keyobj;

        if (allkeys || stringmatchlen(pattern,plen,key,sdslen(key),0)) {
            keyobj = createStringObject(key,sdslen(key));
            /*如果当前key不过期*/
            if (expireIfNeeded(c->db,keyobj) == 0) {
                addReplyBulk(c,keyobj);
                numkeys++;
            }
            decrRefCount(keyobj);
        }
    }
    dictReleaseIterator(di);
    setDeferredMultiBulkLength(c,replylen,numkeys);
}

/* This callback is used by scanGenericCommand in order to collect elements
 * returned by the dictionary iterator into a list. */
void scanCallback(void *privdata, const dictEntry *de) {
    void **pd = (void**) privdata;
    list *keys = pd[0];
    robj *o = pd[1];
    robj *key, *val = NULL;

    if (o == NULL) {
        sds sdskey = dictGetKey(de);
        key = createStringObject(sdskey, sdslen(sdskey));
    } else if (o->type == OBJ_SET) {
        key = dictGetKey(de);
        incrRefCount(key);
    } else if (o->type == OBJ_HASH) {
        key = dictGetKey(de);
        incrRefCount(key);
        val = dictGetVal(de);
        incrRefCount(val);
    } else if (o->type == OBJ_ZSET) {
        key = dictGetKey(de);
        incrRefCount(key);
        val = createStringObjectFromLongDouble(*(double*)dictGetVal(de),0);
    } else {
        serverPanic("Type not handled in SCAN callback.");
    }

    listAddNodeTail(keys, key);
    if (val) listAddNodeTail(keys, val);
}

/* Try to parse a SCAN cursor stored at object 'o':
 * if the cursor is valid, store it as unsigned integer into *cursor and
 * returns C_OK. Otherwise return C_ERR and send an error to the
 * client. */
int parseScanCursorOrReply(client *c, robj *o, unsigned long *cursor) {
    char *eptr;

    /* Use strtoul() because we need an *unsigned* long, so
     * getLongLongFromObject() does not cover the whole cursor space. */
    errno = 0;
    *cursor = strtoul(o->ptr, &eptr, 10);
    if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' || errno == ERANGE)
    {
        addReplyError(c, "invalid cursor");
        return C_ERR;
    }
    return C_OK;
}

/*2.8.0 版本开始*/
/* This command implements SCAN, HSCAN and SSCAN commands.
 * If object 'o' is passed, then it must be a Hash or Set object, otherwise
 * if 'o' is NULL the command will operate on the dictionary associated with
 * the current database.
 *
 * When 'o' is not NULL the function assumes that the first argument in
 * the client arguments vector is a key so it skips it before iterating
 * in order to parse options.
 *
 * In the case of a Hash object the function returns both the field and value
 * of every element on the Hash. */
void scanGenericCommand(client *c, robj *o, unsigned long cursor) {
    int i, j;

    //创建一个keys链表
    list *keys = listCreate();
    listNode *node, *nextnode;
    long count = 10;
    sds pat = NULL;
    int patlen = 0, use_pattern = 0;
    dict *ht;

    /* Object must be NULL (to iterate keys names), or the type of the object
     * must be Set, Sorted Set, or Hash. */
    serverAssert(o == NULL || o->type == OBJ_SET || o->type == OBJ_HASH ||
                o->type == OBJ_ZSET);

    /* Set i to the first option argument. The previous one is the cursor. */
    i = (o == NULL) ? 2 : 3; /* Skip the key argument if needed. */

    /*第一步: 解析options 选项 */
    /* Step 1: Parse options. */
    while (i < c->argc) {
        j = c->argc - i;
        //j >=2 当c->argc 3个参数时 j 就小于2了
        //strcasecmp函数是一个比较函数,用于比较两个不区分大小写的字符串
        //s1和s2中的所有字母字符在比较之前都转换为小写。
        //#include<string.h>
        if (!strcasecmp(c->argv[i]->ptr, "count") && j >= 2) {

            //解析count后面的一个参数
            if (getLongFromObjectOrReply(c, c->argv[i+1], &count, NULL)
                != C_OK)
            {
                goto cleanup;
            }

            if (count < 1) {
                addReply(c,shared.syntaxerr);
                goto cleanup;
            }

            i += 2; //递进2个参数
        } else if (!strcasecmp(c->argv[i]->ptr, "match") && j >= 2) {
            pat = c->argv[i+1]->ptr;
            patlen = sdslen(pat);

            /* The pattern always matches if it is exactly "*", so it is
             * equivalent to disabling it. */
            /* 如果只是*的话 就 */
            use_pattern = !(pat[0] == '*' && patlen == 1);

            i += 2;
        } else {
            addReply(c,shared.syntaxerr);
            goto cleanup;
        }
    }

    /*第二步 迭代集合*/
    /* Step 2: Iterate the collection.
     *
     * Note that if the object is encoded with a ziplist, intset, or any other
     * representation that is not a hash table, we are sure that it is also
     * composed of a small number of elements. So to avoid taking state we
     * just return everything inside the object in a single call, setting the
     * cursor to zero to signal the end of the iteration. */

    /* Handle the case of a hash table. */
    /* https://cloud.tencent.com/developer/article/1554983 */
    ht = NULL;
    if (o == NULL) {
        ht = c->db->dict; //数据库
    } else if (o->type == OBJ_SET && o->encoding == OBJ_ENCODING_HT) {
        ht = o->ptr;
    } else if (o->type == OBJ_HASH && o->encoding == OBJ_ENCODING_HT) {
        ht = o->ptr;
        count *= 2; /* We return key / value for this type. */
    } else if (o->type == OBJ_ZSET && o->encoding == OBJ_ENCODING_SKIPLIST) {
        zset *zs = o->ptr;
        ht = zs->dict;
        count *= 2; /* We return key / value for this type. */
    }

    if (ht) {
        void *privdata[2];
        /*
        我们将最大迭代次数设置为指定COUNT的10倍，因此，如果哈希表处于病态状态(非常稀疏地填充)，
        我们可以避免阻塞过多的时间，而代价是不返回或返回很少的元素
        */
        /* We set the max number of iterations to ten times the specified
         * COUNT, so if the hash table is in a pathological state (very
         * sparsely populated) we avoid to block too much time at the cost
         * of returning no or very few elements. */
        long maxiterations = count*10;

        /* We pass two pointers to the callback: the list to which it will
         * add new elements, and the object containing the dictionary so that
         * it is possible to fetch more data in a type-dependent way. */
        privdata[0] = keys;
        privdata[1] = o;
        do {
            cursor = dictScan(ht, cursor, scanCallback, privdata);
        } while (cursor &&
              maxiterations-- &&
              listLength(keys) < (unsigned long)count);
    } else if (o->type == OBJ_SET) {

        /* 因为 o->encoding != OBJ_ENCODING_HT  */
        int pos = 0;
        int64_t ll;

        while(intsetGet(o->ptr,pos++,&ll))
            listAddNodeTail(keys,createStringObjectFromLongLong(ll));
        cursor = 0;
    } else if (o->type == OBJ_HASH || o->type == OBJ_ZSET) {
        unsigned char *p = ziplistIndex(o->ptr,0);
        unsigned char *vstr;
        unsigned int vlen;
        long long vll;

        while(p) {
            ziplistGet(p,&vstr,&vlen,&vll);
            listAddNodeTail(keys,
                (vstr != NULL) ? createStringObject((char*)vstr,vlen) :
                                 createStringObjectFromLongLong(vll));
            p = ziplistNext(o->ptr,p);
        }
        cursor = 0;
    } else {
        serverPanic("Not handled encoding in SCAN.");
    }

    /* 再次筛选 查询到的元素 */
    /* Step 3: Filter elements. */
    node = listFirst(keys);
    while (node) {
        robj *kobj = listNodeValue(node);
        nextnode = listNextNode(node);
        int filter = 0;

        /* Filter element if it does not match the pattern. */
        if (!filter && use_pattern) {
            if (sdsEncodedObject(kobj)) {
                if (!stringmatchlen(pat, patlen, kobj->ptr, sdslen(kobj->ptr), 0))
                    filter = 1;
            } else {
                char buf[LONG_STR_SIZE];
                int len;

                serverAssert(kobj->encoding == OBJ_ENCODING_INT);
                len = ll2string(buf,sizeof(buf),(long)kobj->ptr);
                if (!stringmatchlen(pat, patlen, buf, len, 0)) filter = 1;
            }
        }

        /* Filter element if it is an expired key. */
        if (!filter && o == NULL && expireIfNeeded(c->db, kobj)) filter = 1;

        /* Remove the element and its associted value if needed. */
        if (filter) {
            decrRefCount(kobj);
            listDelNode(keys, node);
        }

        /* If this is a hash or a sorted set, we have a flat list of
         * key-value elements, so if this element was filtered, remove the
         * value, or skip it if it was not filtered: we only match keys. */
        if (o && (o->type == OBJ_ZSET || o->type == OBJ_HASH)) {
            node = nextnode;
            nextnode = listNextNode(node);
            if (filter) {
                kobj = listNodeValue(node);
                decrRefCount(kobj);
                listDelNode(keys, node);
            }
        }
        node = nextnode;
    }

    /*回复客户端 */
    /* Step 4: Reply to the client. */
    addReplyMultiBulkLen(c, 2);
    addReplyBulkLongLong(c,cursor);

    addReplyMultiBulkLen(c, listLength(keys));
    while ((node = listFirst(keys)) != NULL) {
        robj *kobj = listNodeValue(node);
        addReplyBulk(c, kobj);
        decrRefCount(kobj);
        listDelNode(keys, node);
    }

cleanup:
    listSetFreeMethod(keys,decrRefCountVoid);
    listRelease(keys);
}

/* The SCAN command completely relies on scanGenericCommand. */
void scanCommand(client *c) {
    unsigned long cursor;
    if (parseScanCursorOrReply(c,c->argv[1],&cursor) == C_ERR) return;
    scanGenericCommand(c,NULL,cursor);
}

void dbsizeCommand(client *c) {
    addReplyLongLong(c,dictSize(c->db->dict));
}

void lastsaveCommand(client *c) {
    addReplyLongLong(c,server.lastsave);
}

void typeCommand(client *c) {
    robj *o;
    char *type;

    o = lookupKeyReadWithFlags(c->db,c->argv[1],LOOKUP_NOTOUCH);
    if (o == NULL) {
        type = "none";
    } else {
        switch(o->type) {
        case OBJ_STRING: type = "string"; break;
        case OBJ_LIST: type = "list"; break;
        case OBJ_SET: type = "set"; break;
        case OBJ_ZSET: type = "zset"; break;
        case OBJ_HASH: type = "hash"; break;
        default: type = "unknown"; break;
        }
    }
    addReplyStatus(c,type);
}

void shutdownCommand(client *c) {
    int flags = 0;

    if (c->argc > 2) {
        addReply(c,shared.syntaxerr);
        return;
    } else if (c->argc == 2) {
        if (!strcasecmp(c->argv[1]->ptr,"nosave")) {
            flags |= SHUTDOWN_NOSAVE;
        } else if (!strcasecmp(c->argv[1]->ptr,"save")) {
            flags |= SHUTDOWN_SAVE;
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }
    /* When SHUTDOWN is called while the server is loading a dataset in
     * memory we need to make sure no attempt is performed to save
     * the dataset on shutdown (otherwise it could overwrite the current DB
     * with half-read data).
     *
     * Also when in Sentinel mode clear the SAVE flag and force NOSAVE. */
    if (server.loading || server.sentinel_mode)
        flags = (flags & ~SHUTDOWN_SAVE) | SHUTDOWN_NOSAVE;
    if (prepareForShutdown(flags) == C_OK) exit(0);
    addReplyError(c,"Errors trying to SHUTDOWN. Check logs.");
}

void renameGenericCommand(client *c, int nx) {
    robj *o;
    long long expire;
    int samekey = 0;

    /* When source and dest key is the same, no operation is performed,
     * if the key exists, however we still return an error on unexisting key. */
    if (sdscmp(c->argv[1]->ptr,c->argv[2]->ptr) == 0) samekey = 1;

    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr)) == NULL)
        return;

    if (samekey) {
        addReply(c,nx ? shared.czero : shared.ok);
        return;
    }

    incrRefCount(o);//这里就增加引用计数
    expire = getExpire(c->db,c->argv[1]);
    if (lookupKeyWrite(c->db,c->argv[2]) != NULL) {
        if (nx) {
            decrRefCount(o);
            addReply(c,shared.czero);
            return;
        }
        /* Overwrite: delete the old key before creating the new one
         * with the same name. */
        dbDelete(c->db,c->argv[2]);
    }
    dbAdd(c->db,c->argv[2],o);
    if (expire != -1) setExpire(c->db,c->argv[2],expire);
    dbDelete(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[2]);
    notifyKeyspaceEvent(NOTIFY_GENERIC,"rename_from",
        c->argv[1],c->db->id);
    notifyKeyspaceEvent(NOTIFY_GENERIC,"rename_to",
        c->argv[2],c->db->id);
    server.dirty++;
    addReply(c,nx ? shared.cone : shared.ok);
}

void renameCommand(client *c) {
    renameGenericCommand(c,0);
}

void renamenxCommand(client *c) {
    renameGenericCommand(c,1);
}

void moveCommand(client *c) {
    robj *o;
    redisDb *src, *dst;
    int srcid;
    long long dbid, expire;

    /*move 在集群模式下不允许*/
    if (server.cluster_enabled) {
        addReplyError(c,"MOVE is not allowed in cluster mode");
        return;
    }

    /* Obtain source and target DB pointers */
    src = c->db;
    srcid = c->db->id;

    if (getLongLongFromObject(c->argv[2],&dbid) == C_ERR ||
        dbid < INT_MIN || dbid > INT_MAX ||
        selectDb(c,dbid) == C_ERR)
    {
        addReply(c,shared.outofrangeerr);
        return;
    }
    dst = c->db;
    selectDb(c,srcid); /* Back to the source DB */

    /* If the user is moving using as target the same
     * DB as the source DB it is probably an error. */
    if (src == dst) {
        addReply(c,shared.sameobjecterr);
        return;
    }

    /* Check if the element exists and get a reference */
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (!o) {
        addReply(c,shared.czero);
        return;
    }
    expire = getExpire(c->db,c->argv[1]);

    /* Return zero if the key already exists in the target DB */
    if (lookupKeyWrite(dst,c->argv[1]) != NULL) {
        addReply(c,shared.czero);
        return;
    }
    //在dst数据库中添加键和值
    dbAdd(dst,c->argv[1],o);
    if (expire != -1) setExpire(dst,c->argv[1],expire);
    incrRefCount(o);

    /* OK! key moved, free the entry in the source DB */
    dbDelete(src,c->argv[1]);
    server.dirty++;
    addReply(c,shared.cone);
}

/*-----------------------------------------------------------------------------
 * Expires API
 *----------------------------------------------------------------------------*/

int removeExpire(redisDb *db, robj *key) {
    /* An expire may only be removed if there is a corresponding entry in the
     * main dict. Otherwise, the key will never be freed. */
    serverAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);
    return dictDelete(db->expires,key->ptr) == DICT_OK;
}

void setExpire(redisDb *db, robj *key, long long when) {
    dictEntry *kde, *de;

    /* Reuse the sds from the main dict in the expire dict */
    /*在expire字典中 重用 主字典中的sds */
    kde = dictFind(db->dict,key->ptr);
    serverAssertWithInfo(NULL,key,kde != NULL);
    //查找原来的或者添加
    de = dictReplaceRaw(db->expires,dictGetKey(kde));
    //设置s64
    dictSetSignedIntegerVal(de,when);
}

/* Return the expire time of the specified key, or -1 if no expire
 * is associated with this key (i.e. the key is non volatile) */
long long getExpire(redisDb *db, robj *key) {
    dictEntry *de;

    /*么有过期的字典 直接返回 */
    /* No expire? return ASAP */
    /* 第一个字典 + 第二个字典 的used属性 */
    if (dictSize(db->expires) == 0 ||
       (de = dictFind(db->expires,key->ptr)) == NULL) return -1;

    /* 条目在过期字典中找到，说明也应该在主字典中存在(安全检查) */
    /* The entry was found in the expire dict, this means it should also
     * be present in the main dict (safety check). */
    serverAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);
    return dictGetSignedIntegerVal(de);
}

/*
  传播过期到从服务和aof文件
*/
/* Propagate expires into slaves and the AOF file.
 * When a key expires in the master, a DEL operation for this key is sent
 * to all the slaves and the AOF file if enabled.
 *
 * This way the key expiry is centralized in one place, and since both
 * AOF and the master->slave link guarantee operation ordering, everything
 * will be consistent even if we allow write operations against expiring
 * keys. */
void propagateExpire(redisDb *db, robj *key) {
    robj *argv[2];

    argv[0] = shared.del;
    argv[1] = key;
    incrRefCount(argv[0]);
    incrRefCount(argv[1]);

    if (server.aof_state != AOF_OFF)
        feedAppendOnlyFile(server.delCommand,db->id,argv,2);
    replicationFeedSlaves(server.slaves,db->id,argv,2);

    decrRefCount(argv[0]);
    decrRefCount(argv[1]);
}

int expireIfNeeded(redisDb *db, robj *key) {

    //获取过期时间
    mstime_t when = getExpire(db,key);
    mstime_t now;

    //没有过期时间 就返回
    if (when < 0) return 0; /* No expire for this key */

    //比如在加载rdb文件的时候
    /* Don't expire anything while loading. It will be done later. */
    if (server.loading) return 0;

    /*
       如果我们是在lua脚本的上下文中，我们声称时间被阻塞到Lua脚本开始的时候。
       这样，一个键只能在第一次访问时过期，而不会在脚本执行的中间过期，
       使向slave / AOF的传播保持一致
    */
    /* If we are in the context of a Lua script, we claim that time is
     * blocked to when the Lua script started. This way a key can expire
     * only the first time it is accessed and not in the middle of the
     * script execution, making propagation to slaves / AOF consistent.
     * See issue #1525 on Github for more information. */
    now = server.lua_caller ? server.lua_time_start : mstime();

    /*
      如果我们运行在slave的上下文中，直接返回
      slave的key过期是被master控制的，它会为过期键发送给我们DEL 操作

      我们仍然尝试返回正确的信息给调用者，
      如果我们认为key应该 有效，那么返回0。
      如果我们认为这个时间key过期了 那么我们返回1
    */
    /* If we are running in the context of a slave, return ASAP:
     * the slave key expiration is controlled by the master that will
     * send us synthesized DEL operations for expired keys.
     *
     * Still we try to return the right information to the caller,
     * that is, 0 if we think the key should be still valid, 1 if
     * we think the key is expired at this time. */
    if (server.masterhost != NULL) return now > when;

    /* 当这个键没有过期时，返回0 */
    /* Return when this key has not expired */
    if (now <= when) return 0;

    /* Delete the key */
    /*调用info命令的时候显示的过期键的数量*/
    server.stat_expiredkeys++;
    //传播过期 给aof 和 从服务
    propagateExpire(db,key);
    notifyKeyspaceEvent(NOTIFY_EXPIRED,
        "expired",key,db->id);
    
    //真正删除
    return dbDelete(db,key);
}

/*-----------------------------------------------------------------------------
 * Expires Commands
 *----------------------------------------------------------------------------*/

/* This is the generic command implementation for EXPIRE, PEXPIRE, EXPIREAT
 * and PEXPIREAT. Because the commad second argument may be relative or absolute
 * the "basetime" argument is used to signal what the base time is (either 0
 * for *AT variants of the command, or the current time for relative expires).
 *
 * unit is either UNIT_SECONDS or UNIT_MILLISECONDS, and is only used for
 * the argv[2] parameter. The basetime is always specified in milliseconds. */
void expireGenericCommand(client *c, long long basetime, int unit) {
    robj *key = c->argv[1], *param = c->argv[2];
    long long when; /* unix time in milliseconds when the key will expire. */

    if (getLongLongFromObjectOrReply(c, param, &when, NULL) != C_OK)
        return;

    if (unit == UNIT_SECONDS) when *= 1000;
    when += basetime;

    /* No key, return zero. */
    if (lookupKeyWrite(c->db,key) == NULL) {
        addReply(c,shared.czero);
        return;
    }

    /* EXPIRE with negative TTL, or EXPIREAT with a timestamp into the past
     * should never be executed as a DEL when load the AOF or in the context
     * of a slave instance.
     *
     * Instead we take the other branch of the IF statement setting an expire
     * (possibly in the past) and wait for an explicit DEL from the master. */
    if (when <= mstime() && !server.loading && !server.masterhost) {
        robj *aux;

        serverAssertWithInfo(c,key,dbDelete(c->db,key));
        server.dirty++;

        /* Replicate/AOF this as an explicit DEL. */
        aux = createStringObject("DEL",3);
        rewriteClientCommandVector(c,2,aux,key);
        decrRefCount(aux);
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(NOTIFY_GENERIC,"del",key,c->db->id);
        addReply(c, shared.cone);
        return;
    } else {
        setExpire(c->db,key,when);
        addReply(c,shared.cone);
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(NOTIFY_GENERIC,"expire",key,c->db->id);
        server.dirty++;
        return;
    }
}

void expireCommand(client *c) {
    expireGenericCommand(c,mstime(),UNIT_SECONDS);
}

void expireatCommand(client *c) {
    expireGenericCommand(c,0,UNIT_SECONDS);
}

void pexpireCommand(client *c) {
    expireGenericCommand(c,mstime(),UNIT_MILLISECONDS);
}

void pexpireatCommand(client *c) {
    expireGenericCommand(c,0,UNIT_MILLISECONDS);
}

void ttlGenericCommand(client *c, int output_ms) {
    long long expire, ttl = -1;

    /* If the key does not exist at all, return -2 */
    if (lookupKeyReadWithFlags(c->db,c->argv[1],LOOKUP_NOTOUCH) == NULL) {
        addReplyLongLong(c,-2);
        return;
    }
    /* The key exists. Return -1 if it has no expire, or the actual
     * TTL value otherwise. */
    expire = getExpire(c->db,c->argv[1]);
    if (expire != -1) {
        ttl = expire-mstime();
        if (ttl < 0) ttl = 0;
    }
    //
    if (ttl == -1) {
        //没有过期时间
        addReplyLongLong(c,-1);
    } else {
        addReplyLongLong(c,output_ms ? ttl : ((ttl+500)/1000));
    }
}

//实现了server.h的函数
//TTL是 Time To Live的缩写
//Round Trip Time (简称 RTT，数据包往返两端的时间)
void ttlCommand(client *c) {
    ttlGenericCommand(c, 0);
}

void pttlCommand(client *c) {
    ttlGenericCommand(c, 1);
}

void persistCommand(client *c) {
    if (lookupKeyWrite(c->db,c->argv[1])) {
        if (removeExpire(c->db,c->argv[1])) {
            addReply(c,shared.cone);
            server.dirty++;
        } else {
            addReply(c,shared.czero);
        }
    } else {
        addReply(c,shared.czero);
    }
}

/* TOUCH key1 [key2 key3 ... keyN] */
void touchCommand(client *c) {
    int touched = 0;
    for (int j = 1; j < c->argc; j++)
        if (lookupKeyRead(c->db,c->argv[j]) != NULL) touched++;
    addReplyLongLong(c,touched);
}

/* -----------------------------------------------------------------------------
 * API to get key arguments from commands
 * ---------------------------------------------------------------------------*/

/* The base case is to use the keys position as given in the command table
 * (firstkey, lastkey, step). */
int *getKeysUsingCommandTable(struct redisCommand *cmd,robj **argv, int argc, int *numkeys) {
    int j, i = 0, last, *keys;
    UNUSED(argv);

    if (cmd->firstkey == 0) {
        *numkeys = 0;
        return NULL;
    }
    last = cmd->lastkey;
    if (last < 0) last = argc+last;
    keys = zmalloc(sizeof(int)*((last - cmd->firstkey)+1));
    for (j = cmd->firstkey; j <= last; j += cmd->keystep) {
        serverAssert(j < argc);
        keys[i++] = j;
    }
    *numkeys = i;
    return keys;
}

/* Return all the arguments that are keys in the command passed via argc / argv.
 *
 * The command returns the positions of all the key arguments inside the array,
 * so the actual return value is an heap allocated array of integers. The
 * length of the array is returned by reference into *numkeys.
 *
 * 'cmd' must be point to the corresponding entry into the redisCommand
 * table, according to the command name in argv[0].
 *
 * This function uses the command table if a command-specific helper function
 * is not required, otherwise it calls the command-specific function. */
int *getKeysFromCommand(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    if (cmd->getkeys_proc) {
        return cmd->getkeys_proc(cmd,argv,argc,numkeys);
    } else {
        return getKeysUsingCommandTable(cmd,argv,argc,numkeys);
    }
}

/* Free the result of getKeysFromCommand. */
void getKeysFreeResult(int *result) {
    zfree(result);
}

/* Helper function to extract keys from following commands:
 * ZUNIONSTORE <destkey> <num-keys> <key> <key> ... <key> <options>
 * ZINTERSTORE <destkey> <num-keys> <key> <key> ... <key> <options> */
int *zunionInterGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, *keys;
    UNUSED(cmd);

    num = atoi(argv[2]->ptr);
    /* Sanity check. Don't return any key if the command is going to
     * reply with syntax error. */
    if (num > (argc-3)) {
        *numkeys = 0;
        return NULL;
    }

    /* Keys in z{union,inter}store come from two places:
     * argv[1] = storage key,
     * argv[3...n] = keys to intersect */
    keys = zmalloc(sizeof(int)*(num+1));

    /* Add all key positions for argv[3...n] to keys[] */
    for (i = 0; i < num; i++) keys[i] = 3+i;

    /* Finally add the argv[1] key position (the storage key target). */
    keys[num] = 1;
    *numkeys = num+1;  /* Total keys = {union,inter} keys + storage key */
    return keys;
}

/* Helper function to extract keys from the following commands:
 * EVAL <script> <num-keys> <key> <key> ... <key> [more stuff]
 * EVALSHA <script> <num-keys> <key> <key> ... <key> [more stuff] */
int *evalGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, *keys;
    UNUSED(cmd);

    num = atoi(argv[2]->ptr);
    /* Sanity check. Don't return any key if the command is going to
     * reply with syntax error. */
    if (num > (argc-3)) {
        *numkeys = 0;
        return NULL;
    }

    keys = zmalloc(sizeof(int)*num);
    *numkeys = num;

    /* Add all key positions for argv[3...n] to keys[] */
    for (i = 0; i < num; i++) keys[i] = 3+i;

    return keys;
}

/* Helper function to extract keys from the SORT command.
 *
 * SORT <sort-key> ... STORE <store-key> ...
 *
 * The first argument of SORT is always a key, however a list of options
 * follow in SQL-alike style. Here we parse just the minimum in order to
 * correctly identify keys in the "STORE" option. */
int *sortGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, j, num, *keys, found_store = 0;
    UNUSED(cmd);

    num = 0;
    keys = zmalloc(sizeof(int)*2); /* Alloc 2 places for the worst case. */

    keys[num++] = 1; /* <sort-key> is always present. */

    /* Search for STORE option. By default we consider options to don't
     * have arguments, so if we find an unknown option name we scan the
     * next. However there are options with 1 or 2 arguments, so we
     * provide a list here in order to skip the right number of args. */
    struct {
        char *name;
        int skip;
    } skiplist[] = {
        {"limit", 2},
        {"get", 1},
        {"by", 1},
        {NULL, 0} /* End of elements. */
    };

    for (i = 2; i < argc; i++) {
        for (j = 0; skiplist[j].name != NULL; j++) {
            if (!strcasecmp(argv[i]->ptr,skiplist[j].name)) {
                i += skiplist[j].skip;
                break;
            } else if (!strcasecmp(argv[i]->ptr,"store") && i+1 < argc) {
                /* Note: we don't increment "num" here and continue the loop
                 * to be sure to process the *last* "STORE" option if multiple
                 * ones are provided. This is same behavior as SORT. */
                found_store = 1;
                keys[num] = i+1; /* <store-key> */
                break;
            }
        }
    }
    *numkeys = num + found_store;
    return keys;
}

int *migrateGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, first, *keys;
    UNUSED(cmd);

    /* Assume the obvious form. */
    first = 3;
    num = 1;

    /* But check for the extended one with the KEYS option. */
    if (argc > 6) {
        for (i = 6; i < argc; i++) {
            if (!strcasecmp(argv[i]->ptr,"keys") &&
                sdslen(argv[3]->ptr) == 0)
            {
                first = i+1;
                num = argc-first;
                break;
            }
        }
    }

    keys = zmalloc(sizeof(int)*num);
    for (i = 0; i < num; i++) keys[i] = first+i;
    *numkeys = num;
    return keys;
}

/* Helper function to extract keys from following commands:
 * GEORADIUS key x y radius unit [WITHDIST] [WITHHASH] [WITHCOORD] [ASC|DESC]
 *                             [COUNT count] [STORE key] [STOREDIST key]
 * GEORADIUSBYMEMBER key member radius unit ... options ... */
int *georadiusGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, *keys;
    UNUSED(cmd);

    /* Check for the presence of the stored key in the command */
    int stored_key = -1;
    for (i = 5; i < argc; i++) {
        char *arg = argv[i]->ptr;
        /* For the case when user specifies both "store" and "storedist" options, the
         * second key specified would override the first key. This behavior is kept 
         * the same as in georadiusCommand method.
         */
        if ((!strcasecmp(arg, "store") || !strcasecmp(arg, "storedist")) && ((i+1) < argc)) {
            stored_key = i+1;
            i++;
        }
    }
    num = 1 + (stored_key == -1 ? 0 : 1);

    /* Keys in the command come from two places:
     * argv[1] = key,
     * argv[5...n] = stored key if present
     */
    keys = zmalloc(sizeof(int) * num);

    /* Add all key positions to keys[] */
    keys[0] = 1;
    if(num > 1) {
         keys[1] = stored_key;
    }
    *numkeys = num; 
    return keys;
}

/* Slot to Key API. This is used by Redis Cluster in order to obtain in
 * a fast way a key that belongs to a specified hash slot. This is useful
 * while rehashing the cluster. */
void slotToKeyAdd(robj *key) {
    unsigned int hashslot = keyHashSlot(key->ptr,sdslen(key->ptr));

    zslInsert(server.cluster->slots_to_keys,hashslot,key);
    incrRefCount(key);
}

void slotToKeyDel(robj *key) {
    unsigned int hashslot = keyHashSlot(key->ptr,sdslen(key->ptr));

    zslDelete(server.cluster->slots_to_keys,hashslot,key);
}

void slotToKeyFlush(void) {
    zslFree(server.cluster->slots_to_keys);
    server.cluster->slots_to_keys = zslCreate();
}

unsigned int getKeysInSlot(unsigned int hashslot, robj **keys, unsigned int count) {
    zskiplistNode *n;
    zrangespec range;
    int j = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    n = zslFirstInRange(server.cluster->slots_to_keys, &range);
    while(n && n->score == hashslot && count--) {
        keys[j++] = n->obj;
        n = n->level[0].forward;
    }
    return j;
}

/* Remove all the keys in the specified hash slot.
 * The number of removed items is returned. */
unsigned int delKeysInSlot(unsigned int hashslot) {
    zskiplistNode *n;
    zrangespec range;
    int j = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    n = zslFirstInRange(server.cluster->slots_to_keys, &range);
    while(n && n->score == hashslot) {
        robj *key = n->obj;
        n = n->level[0].forward; /* Go to the next item before freeing it. */
        incrRefCount(key); /* Protect the object while freeing it. */
        dbDelete(&server.db[0],key);
        decrRefCount(key);
        j++;
    }
    return j;
}

unsigned int countKeysInSlot(unsigned int hashslot) {
    zskiplist *zsl = server.cluster->slots_to_keys;
    zskiplistNode *zn;
    zrangespec range;
    int rank, count = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    /* Find first element in range */
    zn = zslFirstInRange(zsl, &range);

    /* Use rank of first element, if any, to determine preliminary count */
    if (zn != NULL) {
        rank = zslGetRank(zsl, zn->score, zn->obj);
        count = (zsl->length - (rank - 1));

        /* Find last element in range */
        zn = zslLastInRange(zsl, &range);

        /* Use rank of last element, if any, to determine the actual count */
        if (zn != NULL) {
            rank = zslGetRank(zsl, zn->score, zn->obj);
            count -= (zsl->length - rank);
        }
    }
    return count;
}
