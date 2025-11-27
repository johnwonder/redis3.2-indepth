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

#ifndef __REDIS_H
#define __REDIS_H
//当多个源文件（.c文件）包含这个redis.h时，对于每个编译单元（源文件及其包含的头文件），编译器只会处理一次redis.h的内容

#include "fmacros.h"
#include "config.h"
#include "solarisfixes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <lua.h>
#include <signal.h>

typedef long long mstime_t; /* millisecond time type. */

#include "ae.h"      /* 事件驱动库 Event driven programming library */
#include "sds.h"     /* 动态安全的字符串 Dynamic safe strings */
#include "dict.h"    /* hash表 Hash tables */
#include "adlist.h"  /* 链表 Linked lists */
#include "zmalloc.h" /* 总的内存使用 total memory usage aware version of malloc/free */
#include "anet.h"    /* 简单方式的网络 Networking the easy way */
#include "ziplist.h" /* 紧链表数据结构 Compact list data structure */
#include "intset.h"  /* 紧整数集合结构 Compact integer set structure */
#include "version.h" /* 版本宏 Version macro */
#include "util.h"    /* 杂项函数在很多地方都很有用 Misc functions useful in many places */
#include "latency.h" /* 延迟监视器API Latency monitor API */
#include "sparkline.h" /* ASCII图形API ASCII graphs API */
#include "quicklist.h"

/* Following includes allow test functions to be called from Redis main() */
#include "zipmap.h"
#include "sha1.h"
#include "endianconv.h"
#include "crc64.h"

/* Error codes */
#define C_OK                    0
#define C_ERR                   -1

/* Static server configuration 静态服务器配置 */
#define CONFIG_DEFAULT_HZ        10      /* 1秒中段10次 中断次数/秒 Time interrupt calls/sec. */
#define CONFIG_MIN_HZ            1 /* 最少频率  1秒1次*/
#define CONFIG_MAX_HZ            500 /* 最多频率 1秒500次*/

/*hz 可以通过config set hz 100修改或者配置文件中设置*/

#define CONFIG_DEFAULT_SERVER_PORT        6379    /*默认TCP端口 TCP port */
#define CONFIG_DEFAULT_TCP_BACKLOG       511     /*TCP 监听积压 TCP listen backlog */
#define CONFIG_DEFAULT_CLIENT_TIMEOUT       0       /* default client timeout: infinite 默认客户端超时时间 无限长 */
#define CONFIG_DEFAULT_DBNUM     16 /*默认数据库数量*/
#define CONFIG_MAX_LINE    1024 /*fgets函数最多读取的字符数*/
#define CRON_DBS_PER_CALL 16 /*一次调用 遍历数据库的数量 */
#define NET_MAX_WRITES_PER_EVENT (1024*64) /*一次事件可写的最大字节数 64kb*/
#define PROTO_SHARED_SELECT_CMDS 10 /*replicationFeedSlaves 的时候使用 省的创建对象了*/
#define OBJ_SHARED_INTEGERS 10000
#define OBJ_SHARED_BULKHDR_LEN 32
#define LOG_MAX_LEN    1024 /* Default maximum length of syslog messages */
#define AOF_REWRITE_PERC  100
#define AOF_REWRITE_MIN_SIZE (64*1024*1024)
#define AOF_REWRITE_ITEMS_PER_CMD 64
#define CONFIG_DEFAULT_SLOWLOG_LOG_SLOWER_THAN 10000
#define CONFIG_DEFAULT_SLOWLOG_MAX_LEN 128
#define CONFIG_DEFAULT_MAX_CLIENTS 10000
#define CONFIG_AUTHPASS_MAX_LEN 512
#define CONFIG_DEFAULT_SLAVE_PRIORITY 100
#define CONFIG_DEFAULT_REPL_TIMEOUT 60
#define CONFIG_DEFAULT_REPL_PING_SLAVE_PERIOD 10
#define CONFIG_RUN_ID_SIZE 40
#define RDB_EOF_MARK_SIZE 40
#define CONFIG_DEFAULT_REPL_BACKLOG_SIZE (1024*1024)    /* 1mb */
#define CONFIG_DEFAULT_REPL_BACKLOG_TIME_LIMIT (60*60)  /* 1 hour */
#define CONFIG_REPL_BACKLOG_MIN_SIZE (1024*16)          /* 16k */
#define CONFIG_BGSAVE_RETRY_DELAY 5 /* Wait a few secs before trying again. */
#define CONFIG_DEFAULT_PID_FILE "/var/run/redis.pid"
#define CONFIG_DEFAULT_SYSLOG_IDENT "redis"
#define CONFIG_DEFAULT_CLUSTER_CONFIG_FILE "nodes.conf"
#define CONFIG_DEFAULT_DAEMONIZE 0
#define CONFIG_DEFAULT_UNIX_SOCKET_PERM 0
#define CONFIG_DEFAULT_TCP_KEEPALIVE 300
#define CONFIG_DEFAULT_PROTECTED_MODE 1
#define CONFIG_DEFAULT_LOGFILE ""
#define CONFIG_DEFAULT_SYSLOG_ENABLED 0
#define CONFIG_DEFAULT_STOP_WRITES_ON_BGSAVE_ERROR 1
#define CONFIG_DEFAULT_RDB_COMPRESSION 1
#define CONFIG_DEFAULT_RDB_CHECKSUM 1
#define CONFIG_DEFAULT_RDB_FILENAME "dump.rdb"
#define CONFIG_DEFAULT_REPL_DISKLESS_SYNC 0
#define CONFIG_DEFAULT_REPL_DISKLESS_SYNC_DELAY 5
#define CONFIG_DEFAULT_SLAVE_SERVE_STALE_DATA 1
#define CONFIG_DEFAULT_SLAVE_READ_ONLY 1
#define CONFIG_DEFAULT_SLAVE_ANNOUNCE_IP NULL
#define CONFIG_DEFAULT_SLAVE_ANNOUNCE_PORT 0
#define CONFIG_DEFAULT_REPL_DISABLE_TCP_NODELAY 0
#define CONFIG_DEFAULT_MAXMEMORY 0
#define CONFIG_DEFAULT_MAXMEMORY_SAMPLES 5
#define CONFIG_DEFAULT_AOF_FILENAME "appendonly.aof"
#define CONFIG_DEFAULT_AOF_NO_FSYNC_ON_REWRITE 0
#define CONFIG_DEFAULT_AOF_LOAD_TRUNCATED 1
#define CONFIG_DEFAULT_ACTIVE_REHASHING 1
#define CONFIG_DEFAULT_AOF_REWRITE_INCREMENTAL_FSYNC 1
#define CONFIG_DEFAULT_MIN_SLAVES_TO_WRITE 0
#define CONFIG_DEFAULT_MIN_SLAVES_MAX_LAG 10
#define NET_IP_STR_LEN 46 /* INET6_ADDRSTRLEN is 46, but we need to be sure */
#define NET_PEER_ID_LEN (NET_IP_STR_LEN+32) /* 必须足够容纳ip:port Must be enough for ip:port */
#define CONFIG_BINDADDR_MAX 16
#define CONFIG_MIN_RESERVED_FDS 32
#define CONFIG_DEFAULT_LATENCY_MONITOR_THRESHOLD 0

#define ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP 20 /* Loopkups per loop. */
#define ACTIVE_EXPIRE_CYCLE_FAST_DURATION 1000 /* Microseconds */
#define ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC 25 /* CPU max % for keys collection */
#define ACTIVE_EXPIRE_CYCLE_SLOW 0
#define ACTIVE_EXPIRE_CYCLE_FAST 1

/* Instantaneous metrics tracking. */
#define STATS_METRIC_SAMPLES 16     /* Number of samples per metric. */
#define STATS_METRIC_COMMAND 0      /* Number of commands executed. */
#define STATS_METRIC_NET_INPUT 1    /* Bytes read to network .*/
#define STATS_METRIC_NET_OUTPUT 2   /* Bytes written to network. */
#define STATS_METRIC_COUNT 3

/* 协议和输入输出关联的定义 Protocol and I/O related defines */
#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16kb output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */
#define AOF_AUTOSYNC_BYTES (1024*1024*32) /* fdatasync every 32MB */

/* When configuring the server eventloop, we setup it so that the total number
 * of file descriptors we can handle are server.maxclients + RESERVED_FDS +
 * a few more to stay safe. Since RESERVED_FDS defaults to 32, we add 96
 * in order to make sure of not over provisioning more than 128 fds. */
#define CONFIG_FDSET_INCR (CONFIG_MIN_RESERVED_FDS+96)

/* Hash table parameters */
#define HASHTABLE_MIN_FILL        10      /* Minimal hash table fill 10% */

/* Command flags. Please check the command table defined in the redis.c file
 * for more information about the meaning of every flag. */
#define CMD_WRITE 1                   /* "w" flag */
#define CMD_READONLY 2                /* "r" flag */
#define CMD_DENYOOM 4                 /* "m" flag */
#define CMD_NOT_USED_1 8              /* no longer used flag */
#define CMD_ADMIN 16                  /* "a" flag */
#define CMD_PUBSUB 32                 /* "p" flag */
#define CMD_NOSCRIPT  64              /* "s" flag */
#define CMD_RANDOM 128                /* "R" flag */
#define CMD_SORT_FOR_SCRIPT 256       /* "S" flag */
#define CMD_LOADING 512               /* "l" flag */
#define CMD_STALE 1024                /* "t" flag */
#define CMD_SKIP_MONITOR 2048         /* "M" flag */
#define CMD_ASKING 4096               /* "k" flag */
#define CMD_FAST 8192                 /* "F" flag */

/* Object types */
#define OBJ_STRING 0
#define OBJ_LIST 1
#define OBJ_SET 2
#define OBJ_ZSET 3
#define OBJ_HASH 4


//可以谈谈什么时候会编码
/*
对象编码。某些类型的对象，如字符串和哈希，可以在内部以多种方式表示。
该对象的encoding字段被设置为其中一个字段
*/
/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
#define OBJ_ENCODING_RAW 0     /* Raw representation 原始表现  StringEncoding*/
#define OBJ_ENCODING_INT 1     /* Encoded as integer 编码为整数  StringEncoding*/
#define OBJ_ENCODING_HT 2      /* Encoded as hash table  编码为哈希表 HashEncoding*/
#define OBJ_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define OBJ_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list 编码为链表 */
#define OBJ_ENCODING_ZIPLIST 5 /* Encoded as ziplist 编码为压缩列表  HashEncoding ZSetEncoding*/
#define OBJ_ENCODING_INTSET 6  /* Encoded as intset 编码为整数集合*/
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist 编码为跳跃链表  ZSetEncoding*/
#define OBJ_ENCODING_EMBSTR 8  /* Embedded sds string encoding 编码为嵌入式sds StringEncoding*/
#define OBJ_ENCODING_QUICKLIST 9 /* Encoded as linked list of ziplists  编码为压缩列表的链表  ListEncoding * /

/* Defines related to the dump file format. To store 32 bits lengths for short
 * keys requires a lot of space, so we check the most significant 2 bits of
 * the first byte to interpreter the length:
 *
 * 00|000000 => if the two MSB are 00 the len is the 6 bits of this byte
 * 01|000000 00000000 =>  01, the len is 14 byes, 6 bits + 8 bits of next byte
 * 10|000000 [32 bit integer] => if it's 10, a full 32 bit len will follow
 * 11|000000 this means: specially encoded object will follow. The six bits
 *           number specify the kind of object that follows.
 *           See the RDB_ENC_* defines.
 *
 * Lengths up to 63 are stored using a single byte, most DB keys, and may
 * values, will fit inside. */
#define RDB_6BITLEN 0
#define RDB_14BITLEN 1
#define RDB_32BITLEN 2
#define RDB_ENCVAL 3
#define RDB_LENERR UINT_MAX

/* When a length of a string object stored on disk has the first two bits
 * set, the remaining two bits specify a special encoding for the object
 * accordingly to the following defines: */
#define RDB_ENC_INT8 0        /* 8 bit signed integer */
#define RDB_ENC_INT16 1       /* 16 bit signed integer */
#define RDB_ENC_INT32 2       /* 32 bit signed integer */
#define RDB_ENC_LZF 3         /* string compressed with FASTLZ */

/* AOF states */
#define AOF_OFF 0             /* AOF is off */
#define AOF_ON 1              /* AOF is on */
#define AOF_WAIT_REWRITE 2    /* AOF waits rewrite to start appending */

/* 客户端标记 Client flags */
#define CLIENT_SLAVE (1<<0)   /* 这个客户端是slave This client is a slave server */
#define CLIENT_MASTER (1<<1)  /* 这个客户端是一个master This client is a master server */
#define CLIENT_MONITOR (1<<2) /* 这个客户端是一个slave监测， This client is a slave monitor, see MONITOR */
#define CLIENT_MULTI (1<<3)   /* This client is in a MULTI context */
#define CLIENT_BLOCKED (1<<4) /*客户端在等待阻塞操作 The client is waiting in a blocking operation */
#define CLIENT_DIRTY_CAS (1<<5) /* 监视键被修改了 所以exec将会失败  Watched keys modified. EXEC will fail. */
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* 写完整个回复后关闭 Close after writing entire reply. */
#define CLIENT_UNBLOCKED (1<<7) /* 该客户端已解除阻塞，并存储在server.unblocked_clients中 This client was unblocked and is stored in
                                  server.unblocked_clients */
#define CLIENT_LUA (1<<8) /* This is a non connected client used by Lua */
#define CLIENT_ASKING (1<<9)     /* Client issued the ASKING command */
#define CLIENT_CLOSE_ASAP (1<<10)/* 立马关闭这个客户端 Close this client ASAP */
#define CLIENT_UNIX_SOCKET (1<<11) /* 通过unix domain socket连接 Client connected via Unix domain socket */
#define CLIENT_DIRTY_EXEC (1<<12)  /* EXEC将在排队时因错误而失败 EXEC will fail for errors while queueing */
#define CLIENT_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
#define CLIENT_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
#define CLIENT_FORCE_REPL (1<<15)  /* 强制复制 Force replication of current cmd. */
#define CLIENT_PRE_PSYNC (1<<16)   /* 实例不理解PSYNC Instance don't understand PSYNC. */
#define CLIENT_READONLY (1<<17)    /* 集群客户端在只读状态 Cluster client is in read-only state. */
#define CLIENT_PUBSUB (1<<18)      /* 客户端在pubsub模式下 Client is in Pub/Sub mode. */
#define CLIENT_PREVENT_AOF_PROP (1<<19)  /* 不要传播给AOF Don't propagate to AOF. */
#define CLIENT_PREVENT_REPL_PROP (1<<20)  /* 不要传播给slave们 Don't propagate to slaves. */
#define CLIENT_PREVENT_PROP (CLIENT_PREVENT_AOF_PROP|CLIENT_PREVENT_REPL_PROP)
#define CLIENT_PENDING_WRITE (1<<21) /* 客户端有输出要发送，但还没有安装写处理程序 Client has output to send but a write
                                        handler is yet njot installed. */
#define CLIENT_REPLY_OFF (1<<22)   /* 不要回复给客户端 Don't send replies to client. */
#define CLIENT_REPLY_SKIP_NEXT (1<<23)  /* 为下个命令设置CLIENT_REPLY_SKIP Set CLIENT_REPLY_SKIP for next cmd */
#define CLIENT_REPLY_SKIP (1<<24)  /* 仅仅不发这个回复 Don't send just this reply. */
#define CLIENT_LUA_DEBUG (1<<25)  /* Run EVAL in debug mode. */
#define CLIENT_LUA_DEBUG_SYNC (1<<26)  /* EVAL debugging without fork() */

/* Client block type (btype field in client structure)
 * if CLIENT_BLOCKED flag is set. */
#define BLOCKED_NONE 0    /* 么有阻塞， Not blocked, no CLIENT_BLOCKED flag set. */
#define BLOCKED_LIST 1    /* BLPOP & co. */
#define BLOCKED_WAIT 2    /* 等待同步复制 WAIT for synchronous replication. */

/*客户端请求类型*/
/* Client request types */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2

/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define CLIENT_TYPE_NORMAL 0 /* Normal req-reply clients + MONITORs */
#define CLIENT_TYPE_SLAVE 1  /* Slaves. */
#define CLIENT_TYPE_PUBSUB 2 /* Clients subscribed to PubSub channels. */
#define CLIENT_TYPE_MASTER 3 /* Master. 主机 */
#define CLIENT_TYPE_OBUF_COUNT 3 /* 输出缓存配置的客户端数量 Number of clients to expose to output
                                    buffer configuration. Just the first
                                    three: normal, slave, pubsub. */

/* 从机复制状态。 Slave replication state. Used in server.repl_state for slaves to remember
 * what to do next. */
#define REPL_STATE_NONE 0 /* 没有活跃的复制 No active replication */
#define REPL_STATE_CONNECT 1 /* 必须连接到master Must connect to master */
#define REPL_STATE_CONNECTING 2 /* 正在连接到master Connecting to master */
/* --- 握手状态 Handshake states, must be ordered --- */
#define REPL_STATE_RECEIVE_PONG 3 /* Wait for PING reply */
#define REPL_STATE_SEND_AUTH 4 /* Send AUTH to master */
#define REPL_STATE_RECEIVE_AUTH 5 /* Wait for AUTH reply */
#define REPL_STATE_SEND_PORT 6 /* Send REPLCONF listening-port */
#define REPL_STATE_RECEIVE_PORT 7 /* Wait for REPLCONF reply */
#define REPL_STATE_SEND_IP 8 /* Send REPLCONF ip-address */
#define REPL_STATE_RECEIVE_IP 9 /* Wait for REPLCONF reply */
#define REPL_STATE_SEND_CAPA 10 /* Send REPLCONF capa */
#define REPL_STATE_RECEIVE_CAPA 11 /* Wait for REPLCONF reply */
#define REPL_STATE_SEND_PSYNC 12 /* Send PSYNC */
#define REPL_STATE_RECEIVE_PSYNC 13 /* Wait for PSYNC reply */
/* --- End of handshake states --- */
#define REPL_STATE_TRANSFER 14 /* Receiving .rdb from master */
#define REPL_STATE_CONNECTED 15 /* Connected to master */

/* State of slaves from the POV of the master. Used in client->replstate.
 * In SEND_BULK and ONLINE state the slave receives new updates
 * in its output queue. In the WAIT_BGSAVE states instead the server is waiting
 * to start the next background saving in order to send updates to it. */
#define SLAVE_STATE_WAIT_BGSAVE_START 6 /* We need to produce a new RDB file. */
#define SLAVE_STATE_WAIT_BGSAVE_END 7 /* Waiting RDB file creation to finish. */
#define SLAVE_STATE_SEND_BULK 8 /* Sending RDB file to slave. */
#define SLAVE_STATE_ONLINE 9 /* RDB file transmitted, sending just updates. */

/* Slave capabilities. */
#define SLAVE_CAPA_NONE 0
//如果从节点发来的是"REPLCONF capa  eof"命令，
//https://blog.csdn.net/weixin_30565199/article/details/94981444
//则将从节点客户端的能力属性slave_capa增加SLAVE_CAPA_EOF标记，表示该从节点支持无硬盘复制
#define SLAVE_CAPA_EOF (1<<0)   /* Can parse the RDB EOF streaming format. */

/* Synchronous read timeout - slave side */
#define CONFIG_REPL_SYNCIO_TIMEOUT 5

/* List related stuff */
#define LIST_HEAD 0
#define LIST_TAIL 1

/*排序操作 Sort operations */
#define SORT_OP_GET 0

/* 日志级别 Log levels */
#define LL_DEBUG 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define LL_RAW (1<<10) /* Modifier to log without timestamp */
#define CONFIG_DEFAULT_VERBOSITY LL_NOTICE

/* Supervision options */
#define SUPERVISED_NONE 0
#define SUPERVISED_AUTODETECT 1
#define SUPERVISED_SYSTEMD 2
#define SUPERVISED_UPSTART 3

/* Anti-warning macro... */
#define UNUSED(V) ((void) V)

#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^32 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */

/* Append only defines */
/* 操作系统的写回：每个命令执行完，先把日志写入 AOF 文件的缓冲区，由操作系统决定何时把缓冲区的内容写入磁盘 */
#define AOF_FSYNC_NO 0
/* 同步写回：每个命令执行完立马写入磁盘 */
#define AOF_FSYNC_ALWAYS 1
/* 每秒写回：每个命令执行完，先把日志写入 AOF 文件的缓冲区，每隔一秒把缓冲区的内容写入磁盘 */
#define AOF_FSYNC_EVERYSEC 2
#define CONFIG_DEFAULT_AOF_FSYNC AOF_FSYNC_EVERYSEC

/* Zip structure related defaults */
#define OBJ_HASH_MAX_ZIPLIST_ENTRIES 512
#define OBJ_HASH_MAX_ZIPLIST_VALUE 64
#define OBJ_SET_MAX_INTSET_ENTRIES 512
#define OBJ_ZSET_MAX_ZIPLIST_ENTRIES 128
#define OBJ_ZSET_MAX_ZIPLIST_VALUE 64

/* List defaults */
#define OBJ_LIST_MAX_ZIPLIST_SIZE -2
#define OBJ_LIST_COMPRESS_DEPTH 0

/* HyperLogLog defines */
#define CONFIG_DEFAULT_HLL_SPARSE_MAX_BYTES 3000

/* Sets operations codes */
#define SET_OP_UNION 0
#define SET_OP_DIFF 1
#define SET_OP_INTER 2

/* https://zhuanlan.zhihu.com/p/419020079 */
/* Redis maxmemory strategies */
#define MAXMEMORY_VOLATILE_LRU 0 /* 当内存不足以容纳新写入数据时，在设置了过期时间的键空间中，移除最近最少使用的key。*/
#define MAXMEMORY_VOLATILE_TTL 1
#define MAXMEMORY_VOLATILE_RANDOM 2 /*当内存不足以容纳新写入数据时，在设置了过期时间的键空间中，随机移除某个key。*/
#define MAXMEMORY_ALLKEYS_LRU 3 /* 当内存不足以容纳新写入数据时，在键空间中，移除最近最少使用的key*/
#define MAXMEMORY_ALLKEYS_RANDOM 4 /*当内存不足以容纳新写入数据时，在键空间中，随机移除某个key。*/
#define MAXMEMORY_NO_EVICTION 5 /*当内存不足以容纳新写入数据时，新写入操作会报错*/
#define CONFIG_DEFAULT_MAXMEMORY_POLICY MAXMEMORY_NO_EVICTION

/* 脚本 Scripting */
#define LUA_SCRIPT_TIME_LIMIT 5000 /* milliseconds */

/* 单位 Units */
#define UNIT_SECONDS 0  ///秒
#define UNIT_MILLISECONDS 1

/* SHUTDOWN flags */
#define SHUTDOWN_NOFLAGS 0      /* No flags. */
#define SHUTDOWN_SAVE 1         /* Force SAVE on SHUTDOWN even if no save
                                   points are configured. */
#define SHUTDOWN_NOSAVE 2       /* Don't SAVE on SHUTDOWN. */

/*命令调用标记， Command call flags, see call() function */
#define CMD_CALL_NONE 0
#define CMD_CALL_SLOWLOG (1<<0)
#define CMD_CALL_STATS (1<<1)
#define CMD_CALL_PROPAGATE_AOF (1<<2)
#define CMD_CALL_PROPAGATE_REPL (1<<3)
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL)
#define CMD_CALL_FULL (CMD_CALL_SLOWLOG | CMD_CALL_STATS | CMD_CALL_PROPAGATE)

/* Command propagation flags, see propagate() function */
#define PROPAGATE_NONE 0
#define PROPAGATE_AOF 1
#define PROPAGATE_REPL 2

/* RDB active child save type. */
#define RDB_CHILD_TYPE_NONE 0
#define RDB_CHILD_TYPE_DISK 1     /* RDB已经写入disk RDB is written to disk. */
#define RDB_CHILD_TYPE_SOCKET 2   /* RDB已经写入slave socket RDB is written to slave socket. */

/*
 Keyspace更改通知类。为了配置目的，每个类都与一个字符相关联
*/
/* Keyspace changes notification classes. Every class is associated with a
 * character for configuration purposes. */
#define NOTIFY_KEYSPACE (1<<0)    /* K */
#define NOTIFY_KEYEVENT (1<<1)    /* E */
#define NOTIFY_GENERIC (1<<2)     /* g */
#define NOTIFY_STRING (1<<3)      /* $ */
#define NOTIFY_LIST (1<<4)        /* l */
#define NOTIFY_SET (1<<5)         /* s */
#define NOTIFY_HASH (1<<6)        /* h hash表*/
#define NOTIFY_ZSET (1<<7)        /* z 有序列表*/
#define NOTIFY_EXPIRED (1<<8)     /* x */
#define NOTIFY_EVICTED (1<<9)     /* e */

//所有 都通知
#define NOTIFY_ALL (NOTIFY_GENERIC | NOTIFY_STRING | NOTIFY_LIST | NOTIFY_SET | NOTIFY_HASH | NOTIFY_ZSET | NOTIFY_EXPIRED | NOTIFY_EVICTED)      /* A */

/* Get the first bind addr or NULL */
#define NET_FIRST_BIND_ADDR (server.bindaddr_count ? server.bindaddr[0] : NULL)


// 运行间隔
// 实际 依赖server.hz
/* Using the following macro you can run code inside serverCron() with the
 * specified period, specified in milliseconds.
 * The actual resolution depends on server.hz. */
#define run_with_period(_ms_) if ((_ms_ <= 1000/server.hz) || !(server.cronloops%((_ms_)/(1000/server.hz))))

/* We can print the stacktrace, so our assert is defined this way: */
#define serverAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_serverAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define serverAssert(_e) ((_e)?(void)0 : (_serverAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define serverPanic(_e) _serverPanic(#_e,__FILE__,__LINE__),_exit(1)

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

/*一个redis对象，能够保存string,list和set的类型*/
/* A redis object, that is a type able to hold a string / list / set */

/*https://blog.csdn.net/WhereIsHeroFrom/article/details/86501571*/
/* The actual Redis Object 真正的redis对象 */
#define LRU_BITS 24
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1) /* Max value of obj->lru */
#define LRU_CLOCK_RESOLUTION 1000 /* LRU clock resolution in ms */
/*
往后走之前需要知道这个 robj，实际上每个redis key/value 啥的，都是一个redisObject，
是一个 C 结构体（在以后介绍）。也如同 Python中，每一个对象也是个PyObject。
redisObject 结构体定义如下，可以算出一个 RedisObject 的大小为 16 个字节，最后的 ptr 指针，
是指向具体的 redis 对象，例如指向一个 sds、quicklist 等背后真的数据结构。
此外还有其它元数据用来记录一个对象的类型、编码、lru / lfu、引用计数器信息。
*/
/*
再包一层对象头的我理解的好处有：
● 对于每个 redis key（键名都是字符串），redis value（值可以有多种底层的数据结构），都表示为一个 redisObject
● 类似 sds 的 header 头，增加类型等字段，可以实现 redis key 的类型检查、编码检查
● 不同的数据数据类型底层实现的数据结构可能不同，用 void *ptr 指向不同的对象，实现多态
● 可以统一对 redisObject 进行分配、共享和销毁（内部再通过类型编码区分）
● 能增加一些元数据成员，例如 refcount / lru 额外存储一些元数据
*/
/*
如图所示，embstr 和 raw 存储形式是这样的，
embstr 中 robj 和 sds 在内存中是连续的，可以使用 malloc 方法一次分配内存，内存释放也只需要一次；
而 raw 存储形式不一样，它会进行两次 malloc 分配，内存释放需要两次调用，robj 和 SDS 在内存地址上一般是不连续的。内存上紧挨的好处有很多，
例如这边能减少 malloc / free 的调用，
读取性能更好，内存碎片率会更低，内存寻址只用一次，访问局部性原理

● embstr：一次 malloc 分配 robj + sdshdr8 + sdslen + \0
● raw：robj 跟 sds 是分别 malloc 的
https://www.yuque.com/enjoy-binbin/blog/tnc0i7#GYK5U
*/
typedef struct redisObject {
    //// 4 bits 表示类型，例如 String / List / Set / Zset / Hash / Module / Stream
    unsigned type:4;
    //// 4 bits 表示编码，例如 String 根据前面的例子可以有 raw / embstr / int 等编码方式
    unsigned encoding:4;

    /*
     Redis 为每个对象仅分配 ‌24位‌ 空间来存储 LRU 时间戳，这是一种典型的内存优化策略
     如果使用完整的 64 位时间戳（如 Unix 时间戳），每个对象将多消耗 5 字节内存。
     在存储数百万对象的系统中，这种空间节省能显著减少整体内存占用
     24位时钟最大值为 16,777,215（约 194 天），在精度和存储成本间取得了平衡

    
    */
    //// (LRU_BITS 24) 24 bits  LRU / LFU 相关
    unsigned lru:LRU_BITS; /*lru时间 跟server.lruclock相关 lru time (relative to server.lruclock) */
    // 对象是可以共享的，这是个引用计数器，32 bits
    int refcount;
    //// 8 bytes (64-bit system)，指向具体实际对象的指针地址
    void *ptr;
} robj;

/*
获取当前LRU时钟的宏
如果当前分辨率低于我们刷新LRU时钟的频率
我们返回预先计算的值，否则我们需要求助于系统调用
LRU_CLOCK_RESOLUTION = 1000

也就是 1秒大于1次(server.hz大于1)  就使用server.lruclock
否则调用getLRUClock()
*/
/* Macro used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a system call. */
#define LRU_CLOCK() ((1000/server.hz <= LRU_CLOCK_RESOLUTION) ? server.lruclock : getLRUClock())

/*
   在栈上初始化的redis对象
*/
/* Macro used to initialize a Redis object allocated on the stack.
 * Note that this macro is taken near the structure definition to make sure
 * we'll update it when the structure is changed, to avoid bugs like
 * bug #85 introduced exactly in this way. */
#define initStaticStringObject(_var,_ptr) do { \
    _var.refcount = 1; \
    _var.type = OBJ_STRING; \
    _var.encoding = OBJ_ENCODING_RAW; \
    _var.ptr = _ptr; \
} while(0)

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
    为了提高LRU近似的质量，我们取了一组键，这些键在freeMemoryIfNeeded（）调用中是很好的移除候选
 *
    排除池中的条目按空闲时间排序，将空闲时间较大的条目放在右边（升序）。
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * Empty entries have the key pointer set to NULL. */
#define MAXMEMORY_EVICTION_POOL_SIZE 16
struct evictionPoolEntry {
    unsigned long long idle;    /* Object idle time. 对象的空闲时间*/
    sds key;                    /* Key name.  键名称*/
};

/*
 Redis数据库表现
 有多个数据库由从0（默认数据库）到最大配置数据库的整数标识
 数据库编号是结构中的“id”字段
*/
/* Redis database representation. There are multiple databases identified
 * by integers from 0 (the default database) up to the max configured
 * database. The database number is the 'id' field in the structure. */
typedef struct redisDb {
    dict *dict;                 /* 数据库键空间 The keyspace for this DB */
    dict *expires;              /* 键的过期时间，字典的键为键，字典的值为过期时间 Timeout of keys with a timeout set */
    dict *blocking_keys;        /* 用来服务诸如BLPOP的命令，记录目前被阻塞的键 Keys with clients waiting for data (BLPOP) */
    dict *ready_keys;           /* 收到PUSH命令的被阻塞的键 Blocked keys that received a PUSH */
    dict *watched_keys;         /* WATCHED keys for MULTI/EXEC CAS */
    struct evictionPoolEntry *eviction_pool;    /* 过期池 Eviction pool of keys */
    int id;                     /* 数据库ID Database ID */
    long long avg_ttl;          /* 数据库键的平均TTL，统计信息 Average TTL, just for stats 统计数据*/
} redisDb;

/* 客户端 事务 状态*/
/* Client MULTI/EXEC state */
typedef struct multiCmd {
    robj **argv;
    int argc;
    struct redisCommand *cmd;
} multiCmd;

typedef struct multiState {
    multiCmd *commands;     /* 事务命令的数组 Array of MULTI commands */
    int count;              /* Multi命令的总数量 Total number of MULTI commands */
    int minreplicas;        /* MINREPLICAS for synchronous replication */
    time_t minreplicas_timeout; /* MINREPLICAS timeout as unixtime. */
} multiState;

/* This structure holds the blocking operation state for a client.
 * The fields used depend on client->btype. */
typedef struct blockingState {
    /* Generic fields. */
    mstime_t timeout;       /* 阻塞操作过期时间。如果unix当前时间 大于 timeout 那么操作就过期 Blocking operation timeout. If UNIX current time
                             * is > timeout then the operation timed out. */

    /* BLOCKED_LIST */
    dict *keys;             /* 等待 终止阻塞操作的keys 比如BLPOP.  
                               The keys we are waiting to terminate a blocking
                             * operation such as BLPOP. Otherwise NULL. */
    robj *target;           /* 应该收到元素的key The key that should receive the element,
                             * for BRPOPLPUSH. */

    /* BLOCKED_WAIT */
    int numreplicas;        /* Number of replicas we are waiting for ACK. */
    long long reploffset;   /* 需要达到的复制偏移 Replication offset to reach. */
} blockingState;

/*
 这个结构体 描述了ready_keys列表中的节点

 在执行每个命令或脚本之后，我们运行这个列表来检查我们是否应该将数据提供给阻塞的客户端，并解除阻塞。
 注意server.ready_keys将不会有重复，
 因为在代表Redis数据库的每个结构中都有字典也称为ready_keys，
 我们确保记住是否已经在服务器中添加了给定的键添加进了ready_keys列表
*/
/* The following structure represents a node in the server.ready_keys list,
 * where we accumulate all the keys that had clients blocked with a blocking
 * operation such as B[LR]POP, but received new data in the context of the
 * last executed command.
 *
 * After the execution of every command or script, we run this list to check
 * if as a result we should serve data to clients blocked, unblocking them.
 * Note that server.ready_keys will not have duplicates as there dictionary
 * also called ready_keys in every structure representing a Redis database,
 * where we make sure to remember if a given key was already added in the
 * server.ready_keys list. */
typedef struct readyList {
    redisDb *db;
    robj *key;
} readyList;

/*
client类对应了3.0版本中的redisClient类。
因为Redis对IO是多路复用的，所以需要为每个客户端连接维护一个状态，
所以client实际上类似于session一样，是在服务器端维护的一个状态
*/
//对于多路复用，我们需要采用每个客户端的状态
//客户端被放在一个链表中
/* With multiplexing we need to take per-client state.
 * Clients are taken in a linked list. */
typedef struct client {
    uint64_t id;            /* 客户端增量唯一ID Client incremental unique ID. */
    int fd;                 /* 客户端套接字 Client socket. */
    redisDb *db;            /* 当前选择的数据库指针 Pointer to currently SELECTed DB. */
    int dictid;             /* 当前数据库id ID of the currently SELECTed DB. */
    robj *name;             /* 通过设置名称来设置 As set by CLIENT SETNAME. */
    sds querybuf;           /* 客户端发送的命令会存储在这里 用于累积客户端查询的缓冲区 Buffer we use to accumulate client queries. */
    size_t querybuf_peak;   /* 最近（100ms或以上）查询大小的峰值 Recent (100ms or more) peak of querybuf size. */
    int argc;               /* 当前命令参数数量 Num of arguments of current command. */
    robj **argv;            /* 当前命令的参数 已经是robj了 Arguments of current command. */
    struct redisCommand *cmd, *lastcmd;  /* 最后执行的命令 Last command executed. */
    int reqtype;            /* 请求协议类型 Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* 待读取的多批量参数个数 Number of multi bulk arguments left to read. */
    long bulklen;           /* 多批量请求中批量参数的长度 Length of bulk argument in multi bulk request. */
    list *reply;            /* 要发送给客户端的应答对象列表。 List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* 回复列表中对象的字节数 Tot bytes of objects in reply list. */
    size_t sentlen;         /* 当前缓冲已经发送的字节数或者正在发送的对象 Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* 客户端创建时间 Client creation time. */
    time_t lastinteraction; /* readQueryFromClient的时候会更新 从客户端读取后 最后活跃时间 Time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    int flags;              /* 客户端标记 Client flags: CLIENT_* macros. */
    int authenticated;      /* 当需要密码的时候不为空 When requirepass is non-NULL. */
    int replstate;          /* 如果客户端是slave的时候 复制状态 Replication state if this is a slave. */
    int repl_put_online_on_ack; /* Install slave write handler on ACK. ACK （Acknowledge character）
                                    即是确认字符，在数据通信中，接收站发给发送站的一种传输类控制字符。表示发来的数据已确认接收无误
                                */
    int repldbfd;           /* 复制数据库文件描述符 Replication DB file descriptor. */
    off_t repldboff;        /* 复制数据库文件offset Replication DB file offset. */
    off_t repldbsize;       /* 复制数据库文件大小 Replication DB file size. */
    sds replpreamble;       /* Replication DB preamble. */
    long long reploff;      /* 复制偏移 Replication offset if this is our master. */
    long long repl_ack_off; /* slave的 复制ack 偏移， Replication ack offset, if this is a slave. */
    long long repl_ack_time;/* slave的 复制ack 时间 Replication ack time, if this is a slave. */
    long long psync_initial_offset; /* FULLRESYNC reply offset other slaves
                                       copying this slave output buffer
                                       should use. */
    char replrunid[CONFIG_RUN_ID_SIZE+1]; /* master的 run id Master run id if is a master. */
    int slave_listening_port; /* As configured with: REPLCONF listening-port */
    char slave_ip[NET_IP_STR_LEN]; /* Optionally given by REPLCONF ip-address */
    int slave_capa;         /*  Slave capabilities: SLAVE_CAPA_* bitwise OR. */
    multiState mstate;      /* 事务状态 MULTI/EXEC state */
    int btype;              /* Type of blocking op if CLIENT_BLOCKED. */
    blockingState bpop;     /* 阻塞状态 blocking state */
    long long woff;         /* 最后写的 全局复制offset Last write global replication offset. */
    list *watched_keys;     /* 为了multi/exec cas被监测的keys Keys WATCHED for MULTI/EXEC CAS */
    dict *pubsub_channels;  /* 客户端感兴趣的频道 channels a client is interested in (SUBSCRIBE) */
    list *pubsub_patterns;  /* 客户端感兴趣的匹配模式 patterns a client is interested in (SUBSCRIBE) */
    sds peerid;             /* Cached peer ID. */

    /* 反应缓冲区 Response buffer */
    int bufpos;
    char buf[PROTO_REPLY_CHUNK_BYTES]; //16kb
} client;

struct saveparam {
    time_t seconds;
    int changes;
};

/*共享对象结构*/
struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space, //9
    *colon, *nullbulk, *nullmultibulk, *queued, //13
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr, //18
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr, //23
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr, //28
    *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk, //34
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop, //40
    *lpush, *emptyscan, *minstring, *maxstring, //44
    *select[PROTO_SHARED_SELECT_CMDS], //10
    *integers[OBJ_SHARED_INTEGERS],  //10000个
    *mbulkhdr[OBJ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[OBJ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};

/* ZSETs use a specialized version of Skiplists */
typedef struct zskiplistNode {
   /* 7。0改为 sds ele : 一个简单动态字符串，存储着对应的成员，成员是唯一的 */
    robj *obj;
     /* score: 节点的分值（成员的权重），用于实现排序，当 score 相同会用 ele 排序，整体是升序的 */
    double score;
    /* backward: 指向后向节点的指针，用于访问表头方向的后继节点（节点的前一个节点）也就是节点的前驱节点，可以实现从表尾向表头遍历。
    一个节点只有第一层会有前驱节点指针，因此跳表中第一层链表是一个双向链表 */
    struct zskiplistNode *backward;
    /* level：层级数组，跳表是一个多层的链表，每一层是由多个节点通过指针连接起来的。
    每个元素代表跳表的一层，也就是用 zskiplistLevel 结构体表示，
    比如说 level[0] 就表示第一层，以此类推。每一层都带有两个属性：指向前驱节点的指针和跨度，
    每次生成一个跳表节点，都会根据随机来确定该节点会有几层 */
    struct zskiplistLevel {
        /*   forward：指向前向节点的指针，用于访问表尾方向的前驱节点（节点的后一个节点），也就是节点的后驱节点，可以实现从表头向表尾遍历 */
        struct zskiplistNode *forward;
        /*   
        span：跨度记录了当前节点和前驱节点之间跨越的距离（跳了几个节点），可以在遍历查找的过程中算出元素在跳表里具体的位置，用来计算排名 rank，
        计算节点索引值。遍历查找的过程中将访问过的跨度累加起来，就能得到节点在跳表中的 rank 
        */
        unsigned int span;
    } level[];
    /* level 数组的设计，利用 C 指针数组的特性，从内存和效率来说都是很优秀的，对比 JDK 的 ConcurrentSkipListMap（里面用了大量的引用和 new 操作），指针数组的优势就很大了 */

    /*。为柔性数组。每个节点的数组长度不一样，
    在生成跳跃表节点时，随机生成一个1～64的值，
    值越大出现的概率越低。 */
} zskiplistNode;

/**
 * 跳跃表
*/
typedef struct zskiplist {
    /* header 指向跳表的表头节点，默认会初始化为最高层，有 32 或者 64 层（源码变更了好几次，目前 7.0 是固定最高 32 层），不作为实际跳表元素节点，是一个类似哑节点的角色 */
    /* tail：指向跳表的表尾节点，是实打实的一个数据节点 */
    struct zskiplistNode *header, *tail;
    /* length：记录跳表的长度，即跳表数据节点数量（不包含表头节点） */
    unsigned long length;
    /* level：整个跳表的层级，指的是目前跳表中，层数最高的那个数据节点的层级（表头节点不参与计算层级）。
    层数一般不高，遍历的时候从最上面往下遍历会有点浪费，
    所以这边会直接记录 level 跳表的最高层级，遍历的时候就从这个 level 开始遍历。 */
    int level;
} zskiplist;

typedef struct zset {
    dict *dict;/* 字典，用于存储 value 和 score 的映射关系，查询 O(1) */
    zskiplist *zsl;/* 真正的跳表 zset skip list */
} zset;

typedef struct clientBufferLimitsConfig {
    /* 硬限制字节数 */
    unsigned long long hard_limit_bytes;
    /*软限制字节数 */
    unsigned long long soft_limit_bytes;
    time_t soft_limit_seconds;
} clientBufferLimitsConfig;

extern clientBufferLimitsConfig clientBufferLimitsDefaults[CLIENT_TYPE_OBUF_COUNT];

/* The redisOp structure defines a Redis Operation, that is an instance of
 * a command with an argument vector, database ID, propagation target
 * (PROPAGATE_*), and command pointer.
 *
 * Currently only used to additionally propagate more commands to AOF/Replication
 * after the propagation of the executed command. */
typedef struct redisOp {
    robj **argv;
    int argc, dbid, target;
    struct redisCommand *cmd;
} redisOp;

/* Defines an array of Redis operations. There is an API to add to this
 * structure in a easy way.
 *
 * redisOpArrayInit();
 * redisOpArrayAppend();
 * redisOpArrayFree();
 */
typedef struct redisOpArray {
    redisOp *ops;
    int numops;
} redisOpArray;

/*-----------------------------------------------------------------------------
 * Global server state
 *----------------------------------------------------------------------------*/

struct clusterState;

/* AIX defines hz to __hz, we don't use this define and in order to allow
 * Redis build on AIX we need to undef it. */
#ifdef _AIX
#undef hz
#endif

struct redisServer {
    /* General */
    pid_t pid;                  /* Main process pid. */ /*主进程id*/
    char *configfile;           /* Absolute config file path, or NULL */ /*配置文件路径*/
    char *executable;           /* Absolute executable file path.  进程文件绝对路径 */
    char **exec_argv;           /* Executable argv vector (copy).  执行参数 */
    int hz;                     /* serverCron() calls frequency in hertz serverCron函数调用频率 */
    redisDb *db;
    dict *commands;             /* Command table */
    dict *orig_commands;        /* Command table before command renaming. */
    aeEventLoop *el;
    unsigned lruclock:LRU_BITS; /* Clock for LRU eviction */
    int shutdown_asap;          /* SHUTDOWN needed ASAP */
    int activerehashing;        /* 在serverCron（）中进行增量散列 Incremental rehash in serverCron() */
    char *requirepass;          /* Pass for AUTH command, or NULL */
    char *pidfile;              /* PID file path */
    int arch_bits;              /* 32 or 64 depending on sizeof(long) */
    int cronloops;              /* Number of times the cron function run */
    char runid[CONFIG_RUN_ID_SIZE+1];  /* initServerConfig 中会初始化 ID always different at every exec.  每个进程的ID总是不同的*/
    int sentinel_mode;          /* checkForSentinelMode 函数中会返回1或者0 如果这个实例是哨兵 那就为true True if this instance is a Sentinel. */
    /* Networking */
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    char *bindaddr[CONFIG_BINDADDR_MAX]; /* 16 Addresses we should bind to */
    int bindaddr_count;         /* 在server.bindaddr中地址的数量 Number of addresses in server.bindaddr[] */
    char *unixsocket;           /* UNIX socket path */
    mode_t unixsocketperm;      /* UNIX socket permission */
    int ipfd[CONFIG_BINDADDR_MAX]; /* tcp socket文件描述符 TCP socket file descriptors */ /*CONFIG_BINDADDR_MAX 默认为16*/
    int ipfd_count;             /* Used slots in ipfd[] */
    int sofd;                   /* Unix socket file descriptor */
    int cfd[CONFIG_BINDADDR_MAX];/* Cluster bus listening socket */
    int cfd_count;              /* Used slots in cfd[] */
    list *clients;              /* List of active clients */ //维护的客户端双端链表
    list *clients_to_close;     /* Clients to close asynchronously */
    list *clients_pending_write; /* 有写的或者安装handler There is to write or install handler. */
    list *slaves, *monitors;    /* salve和monitor的列表 List of slaves and MONITORs */ //双端链表
    client *current_client; /* 崩溃报告 用 Current client, only used on crash report */
    int clients_paused;         /* True if clients are currently paused */
    mstime_t clients_pause_end_time; /* 撤销clients_paused的时间  Time when we undo clients_paused */
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
    dict *migrate_cached_sockets;/* MIGRATE cached sockets */
    uint64_t next_client_id;    /* 下一个客户端唯一标识符 Next client unique ID. Incremental. */
    int protected_mode;         /* Don't accept external connections. */
    /* RDB / AOF loading information */
    int loading;                /* true的时候代表我们正在从磁盘加载数据 We are loading data from disk if true */
    off_t loading_total_bytes;
    off_t loading_loaded_bytes;
    time_t loading_start_time;
    off_t loading_process_events_interval_bytes;
    /* Fast pointers to often looked up command */
    /*快速指针 指向经常查找的命令 */
    struct redisCommand *delCommand, *multiCommand, *lpushCommand, *lpopCommand,
                        *rpopCommand, *sremCommand, *execCommand, *expireCommand,
                        *pexpireCommand;
    /* Fields used only for stats */
    time_t stat_starttime;          /* 服务器启动时间 Server start time */
    long long stat_numcommands;     /* 处理命令的数量 Number of processed commands */
    long long stat_numconnections;  /* 接收连接的数量 Number of connections received */
    long long stat_expiredkeys;     /* 过期键的数量 Number of expired keys */
    long long stat_evictedkeys;     /* 淘汰键的数量 Number of evicted keys (maxmemory) */
    long long stat_keyspace_hits;   /* 成功查找键的次数 Number of successful lookups of keys */
    long long stat_keyspace_misses; /* 失败查找键的次数 Number of failed lookups of keys */
    size_t stat_peak_memory;        /* 最大使用内存记录 Max used memory record */
    long long stat_fork_time;       /* 执行最近一次fork所需的时间 Time needed to perform latest fork() */
    double stat_fork_rate;          /* fork速率，单位为GB/秒 Fork rate in GB/sec. */
    long long stat_rejected_conn;   /*客户端被拒绝了 因为达到最大客户端限制了 Clients rejected because of maxclients */
    long long stat_sync_full;       /* 与slave完全同步的次数 Number of full resyncs with slaves. */
    long long stat_sync_partial_ok; /* Number of accepted PSYNC requests. */
    long long stat_sync_partial_err;/* Number of unaccepted PSYNC requests. */
    list *slowlog;                  /* SLOWLOG list of commands */ //双端链表
    long long slowlog_entry_id;     /* SLOWLOG current entry ID */
    long long slowlog_log_slower_than; /* SLOWLOG time limit (to get logged) */
    unsigned long slowlog_max_len;     /* SLOWLOG max number of items logged */
    size_t resident_set_size;       /* RSS sampled in serverCron(). */
    long long stat_net_input_bytes; /* 从网络读取的字节数 Bytes read from network. */
    long long stat_net_output_bytes; /* Bytes written to network. */
    /* The following two are used to track instantaneous metrics, like
     * number of operations per second, network traffic. */
    struct {
        long long last_sample_time; /* Timestamp of last sample in ms */
        long long last_sample_count;/* Count in last sample */
        long long samples[STATS_METRIC_SAMPLES];
        int idx;
    } inst_metric[STATS_METRIC_COUNT];
    /* Configuration */
    int verbosity;                  /* redis.conf配置文件中的日志级别 Loglevel in redis.conf */
    int maxidletime;                /* Client timeout in seconds */
    int tcpkeepalive;               /* 不是0就是设置SO_KEEPALIVE Set SO_KEEPALIVE if non-zero. */
    int active_expire_enabled;      /* 测试的时候可以禁用 Can be disabled for testing purposes. */
    size_t client_max_querybuf_len; /* 客户端查询缓冲区长度限制 Limit for client query buffer length */
    int dbnum;                      /* Total number of configured DBs */
    int supervised;                 /* 1 if supervised, 0 otherwise. */
    int supervised_mode;            /* See SUPERVISED_* */
    int daemonize;                  /* True if running as a daemon */
    clientBufferLimitsConfig client_obuf_limits[CLIENT_TYPE_OBUF_COUNT];
    /*AOF持久化 AOF persistence */
    int aof_state;                  /* AOF_(ON|OFF|WAIT_REWRITE) */
    int aof_fsync;                  /*  Kind of fsync() policy */
    char *aof_filename;             /* AOF文件名称 Name of the AOF file */
    int aof_no_fsync_on_rewrite;    /* Don't fsync if a rewrite is in prog. */
    int aof_rewrite_perc;           /* Rewrite AOF if % growth is > M and... */
    off_t aof_rewrite_min_size;     /* the AOF file is at least N bytes. */
    off_t aof_rewrite_base_size;    /* AOF size on latest startup or rewrite. */
    off_t aof_current_size;         /* AOF当前大小 AOF current size. */
    int aof_rewrite_scheduled;      /* Rewrite once BGSAVE terminates. */
    pid_t aof_child_pid;            /* 重写进程的PID PID if rewriting process */
    list *aof_rewrite_buf_blocks;   /*持有aof重写过程中的变化 Hold changes during an AOF rewrite. */
    sds aof_buf;      /* AOF buffer, written before entering the event loop */
    int aof_fd;       /* File descriptor of currently selected AOF file */
    int aof_selected_db; /* Currently selected DB in AOF */
    time_t aof_flush_postponed_start; /* UNIX time of postponed AOF flush */
    time_t aof_last_fsync;            /* UNIX time of last fsync() */
    time_t aof_rewrite_time_last;   /* Time used by last AOF rewrite run. */
    time_t aof_rewrite_time_start;  /* 当前AOF的重写开始时间 Current AOF rewrite start time. */
    int aof_lastbgrewrite_status;   /* C_OK or C_ERR */
    unsigned long aof_delayed_fsync;  /* delayed AOF fsync() counter */
    int aof_rewrite_incremental_fsync;/* fsync incrementally while rewriting? */
    int aof_last_write_status;      /* C_OK or C_ERR */
    int aof_last_write_errno;       /* Valid if aof_last_write_status is ERR */
    int aof_load_truncated;         /* Don't stop on unexpected AOF EOF. */
    /* AOF pipes used to communicate between parent and child during rewrite. */
    int aof_pipe_write_data_to_child;
    int aof_pipe_read_data_from_parent;
    int aof_pipe_write_ack_to_parent;
    int aof_pipe_read_ack_from_child;
    int aof_pipe_write_ack_to_child;
    int aof_pipe_read_ack_from_parent;
    int aof_stop_sending_diff;     /* If true stop sending accumulated diffs
                                      to child process. */
    sds aof_child_diff;             /* AOF diff accumulator child side. */
    /* RDB persistence */
    long long dirty;                /* 从最后一次保存开始的变化 Changes to DB from the last save */
    long long dirty_before_bgsave;  /* Used to restore dirty on failed BGSAVE */
    pid_t rdb_child_pid;            /* PID of RDB saving child */
    struct saveparam *saveparams;   /* Save points array for RDB */
    int saveparamslen;              /* Number of saving points */
    char *rdb_filename;             /* Name of RDB file */
    int rdb_compression;            /* 在RDB中使用压缩？ Use compression in RDB? */
    int rdb_checksum;               /* Use RDB checksum? */
    time_t lastsave;                /* Unix time of last successful save */
    time_t lastbgsave_try;          /* Unix time of last attempted bgsave */
    time_t rdb_save_time_last;      /*  Time used by last RDB save run. */
    time_t rdb_save_time_start;     /* Current RDB save start time. */
    int rdb_bgsave_scheduled;       /* 可能的话使用BGSAVE BGSAVE when possible if true. */
    int rdb_child_type;             /* Type of save by active child. */
    int lastbgsave_status;          /* C_OK or C_ERR */
    int stop_writes_on_bgsave_err;  /* Don't allow writes if can't BGSAVE */
    int rdb_pipe_write_result_to_parent; /* RDB pipes used to return the state */
    int rdb_pipe_read_result_from_child; /* of each slave in diskless SYNC. */
    /* Propagation of commands in AOF / replication */
    redisOpArray also_propagate;    /* Additional command to propagate. */
    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* syslog启用 Is syslog enabled? */
    char *syslog_ident;             /* Syslog ident */
    int syslog_facility;            /* Syslog facility */
    /* Replication (master) */
    int slaveseldb;                 /* Last SELECTed DB in replication output */
    long long master_repl_offset;   /* 全局复制偏移 Global replication offset */
    int repl_ping_slave_period;     /* Master pings the slave every N seconds */

    /*
    Redis 的 部分重同步（Partial Resynchronization） 是主从复制中的核心优化机制 —— 简单说：当从库短暂断开连接后重连主库时，无需重新全量同步所有数据，
    只需同步断开期间主库新增的增量数据，大幅减少网络带宽占用和同步耗时，提升主从复制的可靠性和效率。
     当从库短暂断开连接后重连主库时，无需重新全量同步所有数据，只需同步断开期间主库新增的增量数据
    */
    char *repl_backlog;             /* Replication backlog for partial syncs 部分同步的复制囤积*/
    long long repl_backlog_size;    /* Backlog circular buffer size */
    long long repl_backlog_histlen; /* Backlog actual data length */
    long long repl_backlog_idx;     /* Backlog circular buffer current offset */
    long long repl_backlog_off;     /* Replication offset of first byte in the
                                       backlog buffer. */
    time_t repl_backlog_time_limit; /* Time without slaves after the backlog
                                       gets released. */
    time_t repl_no_slaves_since;    /* We have no slaves since that time.
                                       Only valid if server.slaves len is 0. */
    int repl_min_slaves_to_write;   /* 至少多少从服务器需要写入 Min number of slaves to write. */
    int repl_min_slaves_max_lag;    /* Max lag of <count> slaves to write. */
    int repl_good_slaves_count;     /* Number of slaves with lag <= max_lag. */
    int repl_diskless_sync;         /* Send RDB to slaves sockets directly. */
    int repl_diskless_sync_delay;   /* Delay to start a diskless repl BGSAVE. */
    /* Replication (slave) */
    char *masterauth;               /* AUTH with this password with master */
    char *masterhost;               /* 主服务的地址 Hostname of master */
    int masterport;                 /* Port of master */
    int repl_timeout;               /* Timeout after N seconds of master idle */
    client *master;     /* Client that is master for this slave */
    client *cached_master; /* Cached master to be reused for PSYNC. */
    int repl_syncio_timeout; /* Timeout for synchronous I/O calls */
    int repl_state;          /* Replication status if the instance is a slave */
    off_t repl_transfer_size; /* Size of RDB to read from master during sync. */
    off_t repl_transfer_read; /* Amount of RDB read from master during sync. */
    off_t repl_transfer_last_fsync_off; /* Offset when we fsync-ed last time. */
    int repl_transfer_s;     /* Slave -> Master SYNC socket */
    int repl_transfer_fd;    /* Slave -> Master SYNC temp file descriptor */
    char *repl_transfer_tmpfile; /* Slave-> master SYNC temp file name */
    time_t repl_transfer_lastio; /* Unix time of the latest read, for timeout */
    int repl_serve_stale_data; /* Serve stale data when link is down? */
    int repl_slave_ro;          /* Slave is read only? */
    time_t repl_down_since; /* Unix time at which link with master went down */
    int repl_disable_tcp_nodelay;   /* Disable TCP_NODELAY after SYNC? */
    int slave_priority;             /* Reported in INFO and used by Sentinel. */
    int slave_announce_port;        /* Give the master this listening port. */
    char *slave_announce_ip;        /* Give the master this ip address. */
    char repl_master_runid[CONFIG_RUN_ID_SIZE+1];  /* Master run id for PSYNC.*/
    long long repl_master_initial_offset;         /* Master PSYNC offset. */
    /* Replication script cache. */
    dict *repl_scriptcache_dict;        /* SHA1 all slaves are aware of. */
    list *repl_scriptcache_fifo;        /* First in, first out LRU eviction. */
    unsigned int repl_scriptcache_size; /* Max number of elements. */
    /* Synchronous replication. */
    list *clients_waiting_acks;         /* Clients waiting in WAIT command. */
    int get_ack_from_slaves;            /* If true we send REPLCONF GETACK. */
    /* Limits */
    unsigned int maxclients;            /* Max number of simultaneous clients */
    unsigned long long maxmemory;   /* 最大可以使用的内存 Max number of memory bytes to use */
    int maxmemory_policy;           /* 键淘汰策略 Policy for key eviction */
    int maxmemory_samples;          /* 随机抽样的精度 Pricision of random sampling */
    /* Blocked clients */
    unsigned int bpop_blocked_clients; /* Number of clients blocked by lists */
    list *unblocked_clients; /* list of clients to unblock before next loop */
    list *ready_keys;        /* List of readyList structures for BLPOP & co */
    /* Sort parameters - qsort_r() is only available under BSD so we
     * have to take this state global, in order to pass it to sortCompare() */
    int sort_desc;
    int sort_alpha;
    int sort_bypattern;
    int sort_store;
    /* Zip structure config, see redis.conf for more information  */
    size_t hash_max_ziplist_entries;
    size_t hash_max_ziplist_value;
    size_t set_max_intset_entries;
    size_t zset_max_ziplist_entries;
    size_t zset_max_ziplist_value;
    size_t hll_sparse_max_bytes;
    /* List parameters */
    int list_max_ziplist_size;
    int list_compress_depth;
    /* time cache */
    time_t unixtime;        /* Unix time sampled every cron cycle. */
    long long mstime;       /* Like 'unixtime' but with milliseconds resolution. */
    /* Pubsub */
    dict *pubsub_channels;  /* 订阅客户端的映射频道 字典。。键是某个订阅的频道 ，值是一个链表，记录了所有订阅的客户端 Map channels to list of subscribed clients */
    list *pubsub_patterns;  /* A list of pubsub_patterns */
    int notify_keyspace_events; /* 事件通过发布订阅传播 Events to propagate via Pub/Sub. This is an
                                   xor of NOTIFY_... flags. */
    /* Cluster */
    int cluster_enabled;      /* Is cluster enabled? */
    mstime_t cluster_node_timeout; /* Cluster node timeout. */
    char *cluster_configfile; /* Cluster auto-generated config file name. */
    struct clusterState *cluster;  /* 集群状态 State of the cluster */
    int cluster_migration_barrier; /* Cluster replicas migration barrier. */
    int cluster_slave_validity_factor; /* Slave max data age for failover. */
    int cluster_require_full_coverage; /* If true, put the cluster down if
                                          there is at least an uncovered slot.*/
    /* Scripting */
    lua_State *lua; /* 对所有客户端使用一个 The Lua interpreter. We use just one for all clients */
    client *lua_client;   /* 从Lua中查询Redis的“假客户端” The "fake client" to query Redis from Lua */
    client *lua_caller;   /* 客户端正在运行EVAL，或者NULL The client running EVAL right now, or NULL */
    dict *lua_scripts;         /* A dictionary of SHA1 -> Lua scripts */
    mstime_t lua_time_limit;  /* 脚本超时 限制 毫秒 Script timeout in milliseconds */
    mstime_t lua_time_start;  /* 脚本开始的时间 Start time of script, milliseconds time */
    int lua_write_dirty;  /* True if a write command was called during the
                             execution of the current script. */
    int lua_random_dirty; /* True if a random command was called during the
                             execution of the current script. */
    int lua_replicate_commands; /* True if we are doing single commands repl. */
    int lua_multi_emitted;/* True if we already proagated MULTI. */
    int lua_repl;         /* Script replication flags for redis.set_repl(). */
    int lua_timedout;     /* 如果达到脚本执行的时间限制，则为True True if we reached the time limit for script
                             execution. */
    int lua_kill;         /* Kill the script if true. */
    int lua_always_replicate_commands; /* 默认复制类型 Default replication type. */
    /* Latency monitor */
    long long latency_monitor_threshold;
    dict *latency_events;
    /* Assert & bug reporting */
    char *assert_failed;
    char *assert_file;
    int assert_line;
    int bug_report_start; /* True if bug report header was already logged. */
    int watchdog_period;  /* Software watchdog period in ms. 0 = off */
    /* System hardware info */
    size_t system_memory_size;  /* Total memory in system as reported by OS */
};//结尾带分号

typedef struct pubsubPattern {
    client *client;
    robj *pattern;
} pubsubPattern;

typedef void redisCommandProc(client *c);
typedef int *redisGetKeysProc(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);

//redisCommand 是结构体标记
struct redisCommand {
    char *name; /*用指针可以方便计算字节大小*/
    redisCommandProc *proc; //命令实现的函数指针
    int arity;
    char *sflags; /* 字符串表现的标记 Flags as string representation, one char per flag. */
    int flags;    /* 实际的标记 The actual flags, obtained from the 'sflags' field. */
    /* Use a function to determine keys arguments in a command line.
     * Used for Redis Cluster redirect. */
    redisGetKeysProc *getkeys_proc;
    /* What keys should be loaded in background when calling this command? */
    int firstkey; /* 第一个参数是一个key The first argument that's a key (0 = no keys) */
    int lastkey;  /* The last argument that's a key */
    int keystep;  /* 第一个和最后一个键的步骤 The step between first and last key */
    long long microseconds, calls;
};

struct redisFunctionSym {
    char *name;
    unsigned long pointer;
};

typedef struct _redisSortObject {
    robj *obj;
    union {
        double score;
        robj *cmpobj;
    } u;
} redisSortObject;

typedef struct _redisSortOperation {
    int type;
    robj *pattern;
} redisSortOperation;

/* Structure to hold list iteration abstraction. */
typedef struct {
    robj *subject;
    unsigned char encoding;
    unsigned char direction; /* Iteration direction */
    quicklistIter *iter;
} listTypeIterator;

/* Structure for an entry while iterating over a list. */
typedef struct {
    listTypeIterator *li;
    quicklistEntry entry; /* Entry in quicklist */
} listTypeEntry;

/* Structure to hold set iteration abstraction. */
typedef struct {
    robj *subject;
    int encoding;
    int ii; /* intset iterator */
    dictIterator *di;
} setTypeIterator;

/* Structure to hold hash iteration abstraction. Note that iteration over
 * hashes involves both fields and values. Because it is possible that
 * not both are required, store pointers in the iterator to avoid
 * unnecessary memory allocation for fields/values. */
typedef struct {
    robj *subject;
    int encoding;

    unsigned char *fptr, *vptr;

    dictIterator *di;
    dictEntry *de;
} hashTypeIterator;

#define OBJ_HASH_KEY 1
#define OBJ_HASH_VALUE 2

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/
//外部变量 这样其他文件中就可以获取到这个变量
extern struct redisServer server; 
extern struct sharedObjectsStruct shared;
extern dictType setDictType; //集合字典类型
extern dictType zsetDictType; //有序集合字典类型        
extern dictType clusterNodesDictType;
extern dictType clusterNodesBlackListDictType;
extern dictType dbDictType;
extern dictType shaScriptObjectDictType;
extern double R_Zero, R_PosInf, R_NegInf, R_Nan;
extern dictType hashDictType;
extern dictType replScriptCacheDictType;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

/* Utils */
long long ustime(void);
long long mstime(void);
void getRandomHexChars(char *p, unsigned int len);
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l);
void exitFromChild(int retcode);
size_t redisPopcount(void *s, long count);
void redisSetProcTitle(char *title);

/* networking.c -- Networking and Client related operations */
client *createClient(int fd);
void closeTimedoutClients(void);
void freeClient(client *c);
void freeClientAsync(client *c);
void resetClient(client *c);
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask);
void *addDeferredMultiBulkLength(client *c);
void setDeferredMultiBulkLength(client *c, void *node, long length);
void processInputBuffer(client *c);
void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptUnixHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void addReplyBulk(client *c, robj *obj);
void addReplyBulkCString(client *c, const char *s);
void addReplyBulkCBuffer(client *c, const void *p, size_t len);
void addReplyBulkLongLong(client *c, long long ll);
void addReply(client *c, robj *obj);
void addReplySds(client *c, sds s);
void addReplyBulkSds(client *c, sds s);
void addReplyError(client *c, const char *err);
void addReplyStatus(client *c, const char *status);
void addReplyDouble(client *c, double d);
void addReplyHumanLongDouble(client *c, long double d);
void addReplyLongLong(client *c, long long ll);
void addReplyMultiBulkLen(client *c, long length);
void copyClientOutputBuffer(client *dst, client *src);
void *dupClientReplyValue(void *o);
void getClientsMaxBuffers(unsigned long *longest_output_list,
                          unsigned long *biggest_input_buffer);
char *getClientPeerId(client *client);
sds catClientInfoString(sds s, client *client);
sds getAllClientsInfoString(void);
void rewriteClientCommandVector(client *c, int argc, ...);
void rewriteClientCommandArgument(client *c, int i, robj *newval);
void replaceClientCommandVector(client *c, int argc, robj **argv);
unsigned long getClientOutputBufferMemoryUsage(client *c);
void freeClientsInAsyncFreeQueue(void);
void asyncCloseClientOnOutputBufferLimitReached(client *c);
int getClientType(client *c);
int getClientTypeByName(char *name);
char *getClientTypeName(int class);
void flushSlavesOutputBuffers(void);
void disconnectSlaves(void);
int listenToPort(int port, int *fds, int *count);
void pauseClients(mstime_t duration);
int clientsArePaused(void);
int processEventsWhileBlocked(void);
int handleClientsWithPendingWrites(void);
int clientHasPendingReplies(client *c);
void unlinkClient(client *c);
int writeToClient(int fd, client *c, int handler_installed);

#ifdef __GNUC__
void addReplyErrorFormat(client *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void addReplyStatusFormat(client *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void addReplyErrorFormat(client *c, const char *fmt, ...);
void addReplyStatusFormat(client *c, const char *fmt, ...);
#endif

/* List data type */
void listTypeTryConversion(robj *subject, robj *value);
void listTypePush(robj *subject, robj *value, int where);
robj *listTypePop(robj *subject, int where);
unsigned long listTypeLength(robj *subject);
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction);
void listTypeReleaseIterator(listTypeIterator *li);
int listTypeNext(listTypeIterator *li, listTypeEntry *entry);
robj *listTypeGet(listTypeEntry *entry);
void listTypeInsert(listTypeEntry *entry, robj *value, int where);
int listTypeEqual(listTypeEntry *entry, robj *o);
void listTypeDelete(listTypeIterator *iter, listTypeEntry *entry);
void listTypeConvert(robj *subject, int enc);
void unblockClientWaitingData(client *c);
void handleClientsBlockedOnLists(void);
void popGenericCommand(client *c, int where);
void signalListAsReady(redisDb *db, robj *key);

/* MULTI/EXEC/WATCH... */
void unwatchAllKeys(client *c);
void initClientMultiState(client *c);
void freeClientMultiState(client *c);
void queueMultiCommand(client *c);
void touchWatchedKey(redisDb *db, robj *key);
void touchWatchedKeysOnFlush(int dbid);
void discardTransaction(client *c);
void flagTransaction(client *c);
void execCommandPropagateMulti(client *c);

/* Redis object implementation */
void decrRefCount(robj *o);
void decrRefCountVoid(void *o);
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(const char *ptr, size_t len);
robj *createRawStringObject(const char *ptr, size_t len);
robj *createEmbeddedStringObject(const char *ptr, size_t len);
robj *dupStringObject(robj *o);
int isObjectRepresentableAsLongLong(robj *o, long long *llongval);
robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);
robj *createStringObjectFromLongLong(long long value);
robj *createStringObjectFromLongDouble(long double value, int humanfriendly);
robj *createQuicklistObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
int getLongFromObjectOrReply(client *c, robj *o, long *target, const char *msg);
int checkType(client *c, robj *o, int type);
int getLongLongFromObjectOrReply(client *c, robj *o, long long *target, const char *msg);
int getDoubleFromObjectOrReply(client *c, robj *o, double *target, const char *msg);
int getLongLongFromObject(robj *o, long long *target);
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(client *c, robj *o, long double *target, const char *msg);
char *strEncoding(int encoding);
int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
unsigned long long estimateObjectIdleTime(robj *o);
#define sdsEncodedObject(objptr) (objptr->encoding == OBJ_ENCODING_RAW || objptr->encoding == OBJ_ENCODING_EMBSTR)

/* Synchronous I/O with timeout */
ssize_t syncWrite(int fd, char *ptr, ssize_t size, long long timeout);
ssize_t syncRead(int fd, char *ptr, ssize_t size, long long timeout);
ssize_t syncReadLine(int fd, char *ptr, ssize_t size, long long timeout);

/* Replication */
void replicationFeedSlaves(list *slaves, int dictid, robj **argv, int argc);
void replicationFeedMonitors(client *c, list *monitors, int dictid, robj **argv, int argc);
void updateSlavesWaitingBgsave(int bgsaveerr, int type);
void replicationCron(void);
void replicationHandleMasterDisconnection(void);
void replicationCacheMaster(client *c);
void resizeReplicationBacklog(long long newsize);
void replicationSetMaster(char *ip, int port);
void replicationUnsetMaster(void);
void refreshGoodSlavesCount(void);
void replicationScriptCacheInit(void);
void replicationScriptCacheFlush(void);
void replicationScriptCacheAdd(sds sha1);
int replicationScriptCacheExists(sds sha1);
void processClientsWaitingReplicas(void);
void unblockClientWaitingReplicas(client *c);
int replicationCountAcksByOffset(long long offset);
void replicationSendNewlineToMaster(void);
long long replicationGetSlaveOffset(void);
char *replicationGetSlaveName(client *c);
long long getPsyncInitialOffset(void);
int replicationSetupSlaveForFullResync(client *slave, long long offset);

/* Generic persistence functions */
void startLoading(FILE *fp);
void loadingProgress(off_t pos);
void stopLoading(void);

/* RDB persistence */
#include "rdb.h"

/* AOF persistence */
void flushAppendOnlyFile(int force);
void feedAppendOnlyFile(struct redisCommand *cmd, int dictid, robj **argv, int argc);
void aofRemoveTempFile(pid_t childpid);
int rewriteAppendOnlyFileBackground(void);
int loadAppendOnlyFile(char *filename);
void stopAppendOnly(void);
int startAppendOnly(void);
void backgroundRewriteDoneHandler(int exitcode, int bysignal);
void aofRewriteBufferReset(void);
unsigned long aofRewriteBufferSize(void);

/* Sorted sets data type */

/* Struct to hold a inclusive/exclusive range spec by score comparison. */
typedef struct {
    double min, max;
    int minex, maxex; /* are min or max exclusive? */
} zrangespec;

/* Struct to hold an inclusive/exclusive range spec by lexicographic comparison. */
typedef struct {
    robj *min, *max;  /* May be set to shared.(minstring|maxstring) */
    int minex, maxex; /* are min or max exclusive? */
} zlexrangespec;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score);
int zslDelete(zskiplist *zsl, double score, robj *obj);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
double zzlGetScore(unsigned char *sptr);
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
unsigned int zsetLength(robj *zobj);
void zsetConvert(robj *zobj, int encoding);
void zsetConvertToZiplistIfNeeded(robj *zobj, size_t maxelelen);
int zsetScore(robj *zobj, robj *member, double *score);
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);

/*
 核心函数
*/
/* Core functions */
int freeMemoryIfNeeded(void);
int processCommand(client *c);
void setupSignalHandlers(void);
struct redisCommand *lookupCommand(sds name);
struct redisCommand *lookupCommandByCString(char *s);
struct redisCommand *lookupCommandOrOriginal(sds name);
void call(client *c, int flags);
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int flags);
void alsoPropagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int target);
void forceCommandPropagation(client *c, int flags);
void preventCommandPropagation(client *c);
void preventCommandAOF(client *c);
void preventCommandReplication(client *c);
int prepareForShutdown();
#ifdef __GNUC__
void serverLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void serverLog(int level, const char *fmt, ...);
#endif
void serverLogRaw(int level, const char *msg);
void serverLogFromHandler(int level, const char *msg);
void usage(void);
void updateDictResizePolicy(void);
int htNeedsResize(dict *dict);
void populateCommandTable(void);
void resetCommandTableStats(void);
void adjustOpenFilesLimit(void);
void closeListeningSockets(int unlink_unix_socket);
void updateCachedTime(void);
void resetServerStats(void);
unsigned int getLRUClock(void);
const char *evictPolicyToString(void);

#define RESTART_SERVER_NONE 0
#define RESTART_SERVER_GRACEFULLY (1<<0)     /* Do proper shutdown. */
#define RESTART_SERVER_CONFIG_REWRITE (1<<1) /* CONFIG REWRITE before restart.*/
int restartServer(int flags, mstime_t delay);

/* Set data type */
robj *setTypeCreate(robj *value);
int setTypeAdd(robj *subject, robj *value);
int setTypeRemove(robj *subject, robj *value);
int setTypeIsMember(robj *subject, robj *value);
setTypeIterator *setTypeInitIterator(robj *subject);
void setTypeReleaseIterator(setTypeIterator *si);
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele);
robj *setTypeNextObject(setTypeIterator *si);
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele);
unsigned long setTypeRandomElements(robj *set, unsigned long count, robj *aux_set);
unsigned long setTypeSize(robj *subject);
void setTypeConvert(robj *subject, int enc);

/* Hash data type */
void hashTypeConvert(robj *o, int enc);
void hashTypeTryConversion(robj *subject, robj **argv, int start, int end);
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2);
robj *hashTypeGetObject(robj *o, robj *key);
int hashTypeExists(robj *o, robj *key);
int hashTypeSet(robj *o, robj *key, robj *value);
int hashTypeDelete(robj *o, robj *key);
unsigned long hashTypeLength(robj *o);
hashTypeIterator *hashTypeInitIterator(robj *subject);
void hashTypeReleaseIterator(hashTypeIterator *hi);
int hashTypeNext(hashTypeIterator *hi);
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll);
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst);
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what);
robj *hashTypeLookupWriteOrCreate(client *c, robj *key);

/* Pub / Sub */
int pubsubUnsubscribeAllChannels(client *c, int notify);
int pubsubUnsubscribeAllPatterns(client *c, int notify);
void freePubsubPattern(void *p);
int listMatchPubsubPattern(void *a, void *b);
int pubsubPublishMessage(robj *channel, robj *message);

/* Keyspace events notification */
void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid);
int keyspaceEventsStringToFlags(char *classes);
sds keyspaceEventsFlagsToString(int flags);

/* Configuration */
/* config.c 中定义了方法*/
void loadServerConfig(char *filename, char *options);
void appendServerSaveParams(time_t seconds, int changes);
void resetServerSaveParams(void);
struct rewriteConfigState; /* Forward declaration to export API. */
void rewriteConfigRewriteLine(struct rewriteConfigState *state, const char *option, sds line, int force);
int rewriteConfig(char *path);

/* db.c -- Keyspace access API */
int removeExpire(redisDb *db, robj *key);
void propagateExpire(redisDb *db, robj *key);
int expireIfNeeded(redisDb *db, robj *key);
long long getExpire(redisDb *db, robj *key);
void setExpire(redisDb *db, robj *key, long long when);
robj *lookupKey(redisDb *db, robj *key, int flags);
robj *lookupKeyRead(redisDb *db, robj *key);
robj *lookupKeyWrite(redisDb *db, robj *key);
robj *lookupKeyReadOrReply(client *c, robj *key, robj *reply);
robj *lookupKeyWriteOrReply(client *c, robj *key, robj *reply);
robj *lookupKeyReadWithFlags(redisDb *db, robj *key, int flags);
#define LOOKUP_NONE 0
#define LOOKUP_NOTOUCH (1<<0)
void dbAdd(redisDb *db, robj *key, robj *val);
void dbOverwrite(redisDb *db, robj *key, robj *val);
void setKey(redisDb *db, robj *key, robj *val);
int dbExists(redisDb *db, robj *key);
robj *dbRandomKey(redisDb *db);
int dbDelete(redisDb *db, robj *key);
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o);
long long emptyDb(void(callback)(void*));
int selectDb(client *c, int id);
void signalModifiedKey(redisDb *db, robj *key);
void signalFlushedDb(int dbid);
unsigned int getKeysInSlot(unsigned int hashslot, robj **keys, unsigned int count);
unsigned int countKeysInSlot(unsigned int hashslot);
unsigned int delKeysInSlot(unsigned int hashslot);
int verifyClusterConfigWithData(void);
void scanGenericCommand(client *c, robj *o, unsigned long cursor);
int parseScanCursorOrReply(client *c, robj *o, unsigned long *cursor);

/* API to get key arguments from commands */
int *getKeysFromCommand(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
void getKeysFreeResult(int *result);
int *zunionInterGetKeys(struct redisCommand *cmd,robj **argv, int argc, int *numkeys);
int *evalGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
int *sortGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
int *migrateGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
int *georadiusGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);

/* Cluster */
void clusterInit(void);
unsigned short crc16(const char *buf, int len);
unsigned int keyHashSlot(char *key, int keylen);
void clusterCron(void);
void clusterPropagatePublish(robj *channel, robj *message);
void migrateCloseTimedoutSockets(void);
void clusterBeforeSleep(void);

/* Sentinel */
void initSentinelConfig(void);
void initSentinel(void);
void sentinelTimer(void);
char *sentinelHandleConfiguration(char **argv, int argc);
void sentinelIsRunning(void);

/* redis-check-rdb */
int redis_check_rdb(char *rdbfilename);
int redis_check_rdb_main(int argc, char **argv);

/* Scripting */
void scriptingInit(int setup);
int ldbRemoveChild(pid_t pid);
void ldbKillForkedSessions(void);
int ldbPendingChildren(void);

/* Blocked clients */
void processUnblockedClients(void);
void blockClient(client *c, int btype);
void unblockClient(client *c);
void replyToBlockedClientTimedOut(client *c);
int getTimeoutFromObjectOrReply(client *c, robj *object, mstime_t *timeout, int unit);
void disconnectAllBlockedClients(void);

/* Git SHA1 */
char *redisGitSHA1(void);
char *redisGitDirty(void);
uint64_t redisBuildId(void);

/* Commands prototypes */
void authCommand(client *c);
void pingCommand(client *c);
void echoCommand(client *c);
void commandCommand(client *c);
void setCommand(client *c);
void setnxCommand(client *c);
void setexCommand(client *c);
void psetexCommand(client *c);
void getCommand(client *c);
void delCommand(client *c);
void existsCommand(client *c);
void setbitCommand(client *c);
void getbitCommand(client *c);
void bitfieldCommand(client *c);
void setrangeCommand(client *c);
void getrangeCommand(client *c);
void incrCommand(client *c);
void decrCommand(client *c);
void incrbyCommand(client *c);
void decrbyCommand(client *c);
void incrbyfloatCommand(client *c);
void selectCommand(client *c);
void randomkeyCommand(client *c);
void keysCommand(client *c);
void scanCommand(client *c);
void dbsizeCommand(client *c);
void lastsaveCommand(client *c);
void saveCommand(client *c);
void bgsaveCommand(client *c);
void bgrewriteaofCommand(client *c);
void shutdownCommand(client *c);
void moveCommand(client *c);
void renameCommand(client *c);
void renamenxCommand(client *c);
void lpushCommand(client *c);
void rpushCommand(client *c);
void lpushxCommand(client *c);
void rpushxCommand(client *c);
void linsertCommand(client *c);
void lpopCommand(client *c);
void rpopCommand(client *c);
void llenCommand(client *c);
void lindexCommand(client *c);
void lrangeCommand(client *c);
void ltrimCommand(client *c);
void typeCommand(client *c);
void lsetCommand(client *c);
void saddCommand(client *c);
void sremCommand(client *c);
void smoveCommand(client *c);
void sismemberCommand(client *c);
void scardCommand(client *c);
void spopCommand(client *c);
void srandmemberCommand(client *c);
void sinterCommand(client *c);
void sinterstoreCommand(client *c);
void sunionCommand(client *c);
void sunionstoreCommand(client *c);
void sdiffCommand(client *c);
void sdiffstoreCommand(client *c);
void sscanCommand(client *c);
void syncCommand(client *c);
void flushdbCommand(client *c);
void flushallCommand(client *c);
void sortCommand(client *c);
void lremCommand(client *c);
void rpoplpushCommand(client *c);
void infoCommand(client *c);
void mgetCommand(client *c);
void monitorCommand(client *c);
void expireCommand(client *c);
void expireatCommand(client *c);
void pexpireCommand(client *c);
void pexpireatCommand(client *c);
void getsetCommand(client *c);
void ttlCommand(client *c);
void touchCommand(client *c);
void pttlCommand(client *c);
void persistCommand(client *c);
void slaveofCommand(client *c);
void roleCommand(client *c);
void debugCommand(client *c);
void msetCommand(client *c);
void msetnxCommand(client *c);
void zaddCommand(client *c);
void zincrbyCommand(client *c);
void zrangeCommand(client *c);
void zrangebyscoreCommand(client *c);
void zrevrangebyscoreCommand(client *c);
void zrangebylexCommand(client *c);
void zrevrangebylexCommand(client *c);
void zcountCommand(client *c);
void zlexcountCommand(client *c);
void zrevrangeCommand(client *c);
void zcardCommand(client *c);
void zremCommand(client *c);
void zscoreCommand(client *c);
void zremrangebyscoreCommand(client *c);
void zremrangebylexCommand(client *c);
void multiCommand(client *c);
void execCommand(client *c);
void discardCommand(client *c);
void blpopCommand(client *c);
void brpopCommand(client *c);
void brpoplpushCommand(client *c);
void appendCommand(client *c);
void strlenCommand(client *c);
void zrankCommand(client *c);
void zrevrankCommand(client *c);
void hsetCommand(client *c);
void hsetnxCommand(client *c);
void hgetCommand(client *c);
void hmsetCommand(client *c);
void hmgetCommand(client *c);
void hdelCommand(client *c);
void hlenCommand(client *c);
void hstrlenCommand(client *c);
void zremrangebyrankCommand(client *c);
void zunionstoreCommand(client *c);
void zinterstoreCommand(client *c);
void zscanCommand(client *c);
void hkeysCommand(client *c);
void hvalsCommand(client *c);
void hgetallCommand(client *c);
void hexistsCommand(client *c);
void hscanCommand(client *c);
void configCommand(client *c);
void hincrbyCommand(client *c);
void hincrbyfloatCommand(client *c);
void subscribeCommand(client *c);
void unsubscribeCommand(client *c);
void psubscribeCommand(client *c);
void punsubscribeCommand(client *c);
void publishCommand(client *c);
void pubsubCommand(client *c);
void watchCommand(client *c);
void unwatchCommand(client *c);
void clusterCommand(client *c);
void restoreCommand(client *c);
void migrateCommand(client *c);
void askingCommand(client *c);
void readonlyCommand(client *c);
void readwriteCommand(client *c);
void dumpCommand(client *c);
void objectCommand(client *c);
void clientCommand(client *c);
void evalCommand(client *c);
void evalShaCommand(client *c);
void scriptCommand(client *c);
void timeCommand(client *c);
void bitopCommand(client *c);
void bitcountCommand(client *c);
void bitposCommand(client *c);
void replconfCommand(client *c);
void waitCommand(client *c);
void geoencodeCommand(client *c);
void geodecodeCommand(client *c);
void georadiusbymemberCommand(client *c);
void georadiusbymemberroCommand(client *c);
void georadiusCommand(client *c);
void georadiusroCommand(client *c);
void geoaddCommand(client *c);
void geohashCommand(client *c);
void geoposCommand(client *c);
void geodistCommand(client *c);
void pfselftestCommand(client *c);
void pfaddCommand(client *c);
void pfcountCommand(client *c);
void pfmergeCommand(client *c);
void pfdebugCommand(client *c);
void latencyCommand(client *c);
void securityWarningCommand(client *c);

#if defined(__GNUC__)
void *calloc(size_t count, size_t size) __attribute__ ((deprecated));
void free(void *ptr) __attribute__ ((deprecated));
void *malloc(size_t size) __attribute__ ((deprecated));
void *realloc(void *ptr, size_t size) __attribute__ ((deprecated));
#endif

/* Debugging stuff */
void _serverAssertWithInfo(client *c, robj *o, char *estr, char *file, int line);
void _serverAssert(char *estr, char *file, int line);
void _serverPanic(char *msg, char *file, int line);
void bugReportStart(void);
void serverLogObjectDebugInfo(robj *o);
void sigsegvHandler(int sig, siginfo_t *info, void *secret);
sds genRedisInfoString(char *section);
void enableWatchdog(int period);
void disableWatchdog(void);
void watchdogScheduleSignal(int period);
void serverLogHexDump(int level, char *descr, void *value, size_t len);
int memtest_preserving_test(unsigned long *m, size_t bytes, int passes);

#define redisDebug(fmt, ...) \
    printf("DEBUG %s:%d > " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#define redisDebugMark() \
    printf("-- MARK %s:%d --\n", __FILE__, __LINE__)

#endif
