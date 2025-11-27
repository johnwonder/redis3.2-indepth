1:Redis的五种数据类型是由什么数据结构实现的？
2:Redis的字符串数据类型既可以存储字符串（比如"hello world"),又可以存储整数和
  浮点数（比如10086和3.14),甚至是二进制位（使用setbit等命令），Redis在内部是怎么存储这些值的？
3：Redis的一部分命令只能对特定数据类型执行（比如APPEND只能对字符串执行，HSET只能对哈希表执行），
   而另一部分命令却可以对所有数据类型执行（比如DEL,TYPE和EXPIRE),不同的命令在执行时是如何进行类型检查的？
   Redis在内部是否实现了一个类型系统？
4：Redis的数据库是怎样存储各种不同数据类型的键值对的？数据库里面的过期键又是怎样实现自动删除的？
5：除了数据库之外，Redis还拥有发布与订阅，脚本，事务等特性，这些特性又是如何实现的？
6：Redis使用什么模型或者模式来处理客户端的命令请求？一条命令请求从发送到返回需要经过什么步骤？

二进制位操作(bitop),排序，复制，Sentinel和集群等主题，
内部机制（数据库实现，类型系统，事件模型），
单机特性（事务，持久化，Lua脚本，排序，二进制位操作）
多机特性（复制，Sentinel和集群）。



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