第一阶段阅读Redis的数据结构部分，基本位于如下文件中：内存分配 zmalloc.c和zmalloc.h动态字符串 sds.h和sds.c双端链表 adlist.c和adlist.h字典 dict.h和dict.c跳跃表 server.h文件里面关于zskiplist结构和zskiplistNode结构，以及t_zset.c中所有zsl开头的函数，比如 zslCreate、zslInsert、zslDeleteNode等等。基数统计 hyperloglog.c 中的 hllhdr 结构， 以及所有以 hll 开头的函数


第二阶段 熟悉Redis的内存编码结构

整数集合数据结构 intset.h和intset.c
压缩列表数据结构 ziplist.h和ziplist.c

第三阶段 熟悉Redis数据类型的实现对象系统 object.c字符串键 t_string.c列表建 t_list.c散列键 t_hash.c集合键 t_set.c有序集合键 t_zset.c中除 zsl 开头的函数之外的所有函数HyperLogLog键 hyperloglog.c中所有以pf开头的函数


第四阶段 熟悉Redis数据库的实现

数据库实现 redis.h文件中的redisDb结构，以及db.c文件
通知功能 notify.c
RDB持久化 rdb.c
AOF持久化 aof.c

以及一些独立功能模块的实现

发布和订阅 redis.h文件的pubsubPattern结构，以及pubsub.c文件
事务 redis.h文件的multiState结构以及multiCmd结构，multi.c文件

第五阶段 熟悉客户端和服务器端的代码实现事件处理模块 ae.c/ae_epoll.c/ae_evport.c/ae_kqueue.c/ae_select.c网路链接库 anet.c和networking.c服务器端 redis.c客户端 redis-cli.c这个时候可以阅读下面的独立功能模块的代码实现lua脚本 scripting.c慢查询 slowlog.c监视 monitor.c

第六阶段 这一阶段主要是熟悉Redis多机部分的代码实现

复制功能 replication.c
Redis Sentinel sentinel.c
集群 cluster.c

他代码文件介绍关于测试方面的文件有：memtest.c 内存检测redis_benchmark.c 用于redis性能测试的实现。redis_check_aof.c 用于更新日志检查的实现。redis_check_dump.c 用于本地数据库检查的实现。testhelp.c 一个C风格的小型测试框架。

一些工具类的文件如下：bitops.c GETBIT、SETBIT 等二进制位操作命令的实现debug.c 用于调试时使用endianconv.c 高低位转换，不同系统，高低位顺序不同help.h  辅助于命令的提示信息lzf_c.c 压缩算法系列lzf_d.c  压缩算法系列rand.c 用于产生随机数release.c 用于发布时使用sha1.c sha加密算法的实现util.c  通用工具方法crc64.c 循环冗余校验sort.c SORT命令的实现一些封装类的代码实现：bio.c background I/O的意思，开启后台线程用的latency.c 延迟类migrate.c 命令迁移类，包括命令的还原迁移等pqsort.c  排序算法类rio.c redis定义的一个I/O类syncio.c 用于同步Socket和文件I/O操作


作者：Fynn Zhang
链接：https://www.zhihu.com/question/28677076/answer/134193549
来源：知乎
著作权归作者所有。商业转载请联系作者获得授权，非商业转载请注明出处。