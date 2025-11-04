#ifndef __CLUSTER_H
#define __CLUSTER_H

/*-----------------------------------------------------------------------------
 * Redis cluster data structures, defines, exported API.
 *----------------------------------------------------------------------------*/

#define CLUSTER_SLOTS 16384
#define CLUSTER_OK 0          /* Everything looks ok */
#define CLUSTER_FAIL 1        /* The cluster can't work */
#define CLUSTER_NAMELEN 40    /* sha1 hex length */
#define CLUSTER_PORT_INCR 10000 /* Cluster port = baseport + PORT_INCR */

/*
   以下定义是时间量，有时表示为节点超时值的乘数（当以MULT结尾时）
*/
/* The following defines are amount of time, sometimes expressed as
 * multiplicators of the node timeout value (when ending with MULT). */
#define CLUSTER_DEFAULT_NODE_TIMEOUT 15000
#define CLUSTER_DEFAULT_SLAVE_VALIDITY 10 /* Slave max data age factor. */
#define CLUSTER_DEFAULT_REQUIRE_FULL_COVERAGE 1
#define CLUSTER_FAIL_REPORT_VALIDITY_MULT 2 /* Fail report validity. */
#define CLUSTER_FAIL_UNDO_TIME_MULT 2 /* Undo fail if master is back. */
#define CLUSTER_FAIL_UNDO_TIME_ADD 10 /* Some additional time. */
#define CLUSTER_FAILOVER_DELAY 5 /* Seconds */
#define CLUSTER_DEFAULT_MIGRATION_BARRIER 1
#define CLUSTER_MF_TIMEOUT 5000 /* Milliseconds to do a manual failover. */
#define CLUSTER_MF_PAUSE_MULT 2 /* Master pause manual failover mult. */
#define CLUSTER_SLAVE_MIGRATION_DELAY 5000 /* Delay for slave migration. */

/* Redirection errors returned by getNodeByQuery(). */
#define CLUSTER_REDIR_NONE 0          /* 节点可以响应请求 Node can serve the request. */
#define CLUSTER_REDIR_CROSS_SLOT 1    /* -CROSSSLOT request. */
#define CLUSTER_REDIR_UNSTABLE 2      /* -TRYAGAIN redirection required */
#define CLUSTER_REDIR_ASK 3           /* -ASK redirection required. */
#define CLUSTER_REDIR_MOVED 4         /* -MOVED redirection required. */
#define CLUSTER_REDIR_DOWN_STATE 5    /* -CLUSTERDOWN, global state. */
#define CLUSTER_REDIR_DOWN_UNBOUND 6  /* -CLUSTERDOWN, unbound slot. */

struct clusterNode;

/* clusterLink encapsulates everything needed to talk with a remote node. */
typedef struct clusterLink {
    mstime_t ctime;             /* 创建时间 Link creation time */
    int fd;                     /* tcp socket文件描述符 TCP socket file descriptor */
    sds sndbuf;                 /* 发送的包缓存 Packet send buffer */
    sds rcvbuf;                 /* 包接收缓冲 Packet reception buffer */
    struct clusterNode *node;   /* 跟这个link相关的集群节点 Node related to this link if any, or NULL */
} clusterLink;

/* Cluster node flags and macros. */
#define CLUSTER_NODE_MASTER 1     /* 节点是master The node is a master */
#define CLUSTER_NODE_SLAVE 2      /* 节点是slave The node is a slave */
#define CLUSTER_NODE_PFAIL 4      /* Failure? Need acknowledge */
#define CLUSTER_NODE_FAIL 8       /* The node is believed to be malfunctioning */
#define CLUSTER_NODE_MYSELF 16    /* This node is myself */
#define CLUSTER_NODE_HANDSHAKE 32 /* We have still to exchange the first ping */
#define CLUSTER_NODE_NOADDR   64  /* We don't know the address of this node */
#define CLUSTER_NODE_MEET 128     /* Send a MEET message to this node */
#define CLUSTER_NODE_MIGRATE_TO 256 /* Master elegible for replica migration. */
#define CLUSTER_NODE_NULL_NAME "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"

#define nodeIsMaster(n) ((n)->flags & CLUSTER_NODE_MASTER)
#define nodeIsSlave(n) ((n)->flags & CLUSTER_NODE_SLAVE)
#define nodeInHandshake(n) ((n)->flags & CLUSTER_NODE_HANDSHAKE)
#define nodeHasAddr(n) (!((n)->flags & CLUSTER_NODE_NOADDR))
#define nodeWithoutAddr(n) ((n)->flags & CLUSTER_NODE_NOADDR)
#define nodeTimedOut(n) ((n)->flags & CLUSTER_NODE_PFAIL)
#define nodeFailed(n) ((n)->flags & CLUSTER_NODE_FAIL)

/* Reasons why a slave is not able to failover. */
#define CLUSTER_CANT_FAILOVER_NONE 0
#define CLUSTER_CANT_FAILOVER_DATA_AGE 1
#define CLUSTER_CANT_FAILOVER_WAITING_DELAY 2
#define CLUSTER_CANT_FAILOVER_EXPIRED 3
#define CLUSTER_CANT_FAILOVER_WAITING_VOTES 4
#define CLUSTER_CANT_FAILOVER_RELOG_PERIOD (60*5) /* seconds. */

/* This structure represent elements of node->fail_reports. */
typedef struct clusterNodeFailReport {
    struct clusterNode *node;  /* Node reporting the failure condition. */
    mstime_t time;             /* Time of the last report from this node. */
} clusterNodeFailReport;

typedef struct clusterNode {
    mstime_t ctime; /* Node object creation time.  节点对象创建的时间*/
    char name[CLUSTER_NAMELEN]; /* 节点名称  Node name, hex string, sha1-size */
    int flags;      /* CLUSTER_NODE_... */
    uint64_t configEpoch; /* Last configEpoch observed for this node */
    unsigned char slots[CLUSTER_SLOTS/8]; /* slots handled by this node */
    int numslots;   /* Number of slots handled by this node */
    int numslaves;  /* 从节点的数量，如果这个是master的话 Number of slave nodes, if this is a master */
    struct clusterNode **slaves; /* 指向从节点的指针集合 pointers to slave nodes */
    struct clusterNode *slaveof; /* pointer to the master node. Note that it
                                    may be NULL even if the node is a slave
                                    if we don't have the master node in our
                                    tables. */
    mstime_t ping_sent;      /* 发送最后ping 的unix时间戳 Unix time we sent latest ping */
    mstime_t pong_received;  /* 收到pong的时间 Unix time we received the pong */
    mstime_t fail_time;      /* fail标记设置的时间 Unix time when FAIL flag was set */
    mstime_t voted_time;     /* 我们为这个master的一个从机投票的最新时间 Last time we voted for a slave of this master */
    mstime_t repl_offset_time;  /* Unix time we received offset for this node */
    mstime_t orphaned_time;     /* Starting time of orphaned master condition */
    long long repl_offset;      /* Last known repl offset for this node. */
    char ip[NET_IP_STR_LEN];  /* Latest known IP address of this node */
    int port;                   /* Latest known port of this node */
    clusterLink *link;          /* TCP/IP link with this node */
    list *fail_reports;         /* List of nodes signaling this as failing */
} clusterNode;

/*
  在server.cluster中会保存集群状态clusterState结构体，
  是集群中最主要的数据结构，记录集群状态，其中跟槽有关的属性是：
*/
typedef struct clusterState {

    /*当前节点*/
    clusterNode *myself;  /* This node  当前节点*/
    uint64_t currentEpoch;
    int state;            /* 集群状态 CLUSTER_OK, CLUSTER_FAIL, ... */
    int size;             /* 至少有一个槽位的主节点个数 Num of master nodes with at least one slot */
    dict *nodes;          /* 名称->集群节点的 hash表 Hash table of name -> clusterNode structures */
    dict *nodes_black_list; /* Nodes we don't re-add for a few seconds. */

    /*当前节点负责的对应槽位，正在迁出到哪个节点*/
    clusterNode *migrating_slots_to[CLUSTER_SLOTS]; //CLUSTER_SLOTS = 16384

    /*当前节点正在从对应节点，迁入对应槽位*/
    clusterNode *importing_slots_from[CLUSTER_SLOTS];

    //slots数组记录了每个槽位，在当前节点的视角里，分别由集群中哪个节点负责，
    //比如server->cluster.slots[0] = node 代表0号槽由node节点负责。
    clusterNode *slots[CLUSTER_SLOTS]; //槽数组

    /*
      5.0以前是一个skip list,跳表中以槽位号座位分数进行排序。
      5.0改成了rax tree,前缀树
      7.0改成了dict元数据,每个槽后面是挂了一个字典，字典里
      存储了对应槽拥有的所有键。新实现下可以节省大概20%左右的内存，并且添加和删除键的写性能能
      提升50%左右，不过访问和更新现有键的性能会大概降低5-10%左右。
    
      通过这些属性，可以快速知道某个槽位由哪个节点负责，以及它的迁入迁出对象节点，
      以及某个键会落在哪个槽位上。

      skiplist实现的 slots_to_keys:
      在dbAdd中，如果是集群模式会调用slotToKeyAdd往跳表中添加节点：计算出哈希槽，然后把它作为分数，
       把键作为成员，插入到跳表中。

       在dbDelete中，如果是集群模式会调用slotToKeyDel从跳表中删除节点：一样计算出哈希槽，用分数和成员去匹配跳表
       节点进行删除。
    */
    zskiplist *slots_to_keys;
    /*以下字段用于在选举中获取从属状态*/
    /* The following fields are used to take the slave state on elections. */
    mstime_t failover_auth_time; /* Time of previous or next election. */
    int failover_auth_count;    /* 到目前为止收到的票数 Number of votes received so far. */
    int failover_auth_sent;     /* 是否已经询问投票 True if we already asked for votes. */
    int failover_auth_rank;     /* 当前授权请求的从属级别 This slave rank for current auth request. */
    uint64_t failover_auth_epoch; /* 当前选举的epoch Epoch of the current election. */
    int cant_failover_reason;   /* 为什么从服务器当前不能进行故障转移 Why a slave is currently not able to
                                   failover. See the CANT_FAILOVER_* macros. */
    /* Manual failover state in common. 手动故障转移状态 */
    mstime_t mf_end;            /* Manual failover time limit (ms unixtime).
                                   It is zero if there is no MF in progress. */
    /* Manual failover state of master.  主服务器的手动故障转移状态 */
    clusterNode *mf_slave;      /* Slave performing the manual failover. */
    /* Manual failover state of slave. */
    long long mf_master_offset; /* Master offset the slave needs to start MF
                                   or zero if stil not received. */
    int mf_can_start;           /* 如果是非零信号，则表示手动故障转移可以开始请求主投票 If non-zero signal that the manual failover
                                   can start requesting masters vote. */

    /* 不过比如在系统时间方面，epoch指的是一个特定的起始时间点 */
    /* master使用以下字段在选举中获取状态 */
    /* The followign fields are used by masters to take state on elections. */
    uint64_t lastVoteEpoch;     /* 最后一次投票通过的时代 Epoch of the last vote granted. */
    int todo_before_sleep; /*在clusterBeforeSleep()之前调用 Things to do in clusterBeforeSleep(). */
    long long stats_bus_messages_sent;  /* Num of msg sent via cluster bus. */
    long long stats_bus_messages_received; /* Num of msg rcvd via cluster bus.*/
} clusterState;

/* clusterState todo_before_sleep flags. */
#define CLUSTER_TODO_HANDLE_FAILOVER (1<<0)
#define CLUSTER_TODO_UPDATE_STATE (1<<1)
#define CLUSTER_TODO_SAVE_CONFIG (1<<2)
#define CLUSTER_TODO_FSYNC_CONFIG (1<<3)

/* Redis cluster messages header */

/* Note that the PING, PONG and MEET messages are actually the same exact
 * kind of packet. PONG is the reply to ping, in the exact format as a PING,
 * while MEET is a special PING that forces the receiver to add the sender
 * as a node (if it is not already in the list). */
#define CLUSTERMSG_TYPE_PING 0          /* Ping */
#define CLUSTERMSG_TYPE_PONG 1          /* Pong (reply to Ping) */
#define CLUSTERMSG_TYPE_MEET 2          /* Meet "let's join" message */
#define CLUSTERMSG_TYPE_FAIL 3          /* Mark node xxx as failing */
#define CLUSTERMSG_TYPE_PUBLISH 4       /* Pub/Sub Publish propagation */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST 5 /* May I failover? */
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK 6     /* Yes, you have my vote */
#define CLUSTERMSG_TYPE_UPDATE 7        /* Another node slots configuration */
#define CLUSTERMSG_TYPE_MFSTART 8       /* Pause clients for manual failover */

/* Initially we don't know our "name", but we'll find it once we connect
 * to the first node, using the getsockname() function. Then we'll use this
 * address for all the next messages. */
typedef struct {
    char nodename[CLUSTER_NAMELEN];
    uint32_t ping_sent;
    uint32_t pong_received;
    char ip[NET_IP_STR_LEN];  /* IP address last time it was seen */
    uint16_t port;              /* port last time it was seen */
    uint16_t flags;             /* node->flags copy */
    uint16_t notused1;          /* Some room for future improvements. */
    uint32_t notused2;
} clusterMsgDataGossip;

typedef struct {
    char nodename[CLUSTER_NAMELEN];
} clusterMsgDataFail;

typedef struct {
    uint32_t channel_len;
    uint32_t message_len;
    /* We can't reclare bulk_data as bulk_data[] since this structure is
     * nested. The 8 bytes are removed from the count during the message
     * length computation. */
    unsigned char bulk_data[8];
} clusterMsgDataPublish;

typedef struct {
    uint64_t configEpoch; /* Config epoch of the specified instance. */
    char nodename[CLUSTER_NAMELEN]; /* Name of the slots owner. */
    unsigned char slots[CLUSTER_SLOTS/8]; /* Slots bitmap. */
} clusterMsgDataUpdate;

union clusterMsgData {
    /* PING, MEET and PONG */
    struct {
        /* Array of N clusterMsgDataGossip structures */
        clusterMsgDataGossip gossip[1];
    } ping;

    /* FAIL */
    struct {
        clusterMsgDataFail about;
    } fail;

    /* PUBLISH */
    struct {
        clusterMsgDataPublish msg;
    } publish;

    /* UPDATE */
    struct {
        clusterMsgDataUpdate nodecfg;
    } update;
};

#define CLUSTER_PROTO_VER 0 /* Cluster bus protocol version. */

typedef struct {
    char sig[4];        /* Siganture "RCmb" (Redis Cluster message bus). */
    uint32_t totlen;    /* Total length of this message */
    uint16_t ver;       /* Protocol version, currently set to 0. */
    uint16_t notused0;  /* 2 bytes not used. */
    uint16_t type;      /* Message type */
    uint16_t count;     /* Only used for some kind of messages. */
    uint64_t currentEpoch;  /* The epoch accordingly to the sending node. */
    uint64_t configEpoch;   /* The config epoch if it's a master, or the last
                               epoch advertised by its master if it is a
                               slave. */
    uint64_t offset;    /* Master replication offset if node is a master or
                           processed replication offset if node is a slave. */
    char sender[CLUSTER_NAMELEN]; /* Name of the sender node */
    unsigned char myslots[CLUSTER_SLOTS/8];
    char slaveof[CLUSTER_NAMELEN];
    char notused1[32];  /* 32 bytes reserved for future usage. */
    uint16_t port;      /* Sender TCP base port */
    uint16_t flags;     /* Sender node flags */
    unsigned char state; /* Cluster state from the POV of the sender */
    unsigned char mflags[3]; /* Message flags: CLUSTERMSG_FLAG[012]_... */
    union clusterMsgData data;
} clusterMsg;

#define CLUSTERMSG_MIN_LEN (sizeof(clusterMsg)-sizeof(union clusterMsgData))

/* Message flags better specify the packet content or are used to
 * provide some information about the node state. */
#define CLUSTERMSG_FLAG0_PAUSED (1<<0) /* Master paused for manual failover. */
#define CLUSTERMSG_FLAG0_FORCEACK (1<<1) /* Give ACK to AUTH_REQUEST even if
                                            master is up. */

/* ---------------------- API exported outside cluster.c -------------------- */
clusterNode *getNodeByQuery(client *c, struct redisCommand *cmd, robj **argv, int argc, int *hashslot, int *ask);
int clusterRedirectBlockedClientIfNeeded(client *c);
void clusterRedirectClient(client *c, clusterNode *n, int hashslot, int error_code);

#endif /* __CLUSTER_H */
