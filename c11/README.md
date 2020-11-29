

# Linux中的调度相关

## 调度策略与调度类

一种称为**实时进程**，也就是需要尽快执行返回结果的那种。这就好比我们是一家公司，接到的客户项目需求就会有很多种。有些客户的项目需求比较急，比如一定要在一两个月内完成的这种，客户会加急加钱，那这种客户的优先级就会比较高。

另一种是**普通进程**，大部分的进程其实都是这种。这就好比，大部分客户的项目都是普通的需求，可以按照正常流程完成，优先级就没实时进程这么高，但是人家肯定也有确定的交付日期。



## 1、调度策略选择 policy

在 task_struct 中，有一个成员变量，我们叫**调度策略**。 

```
unsigned int policy;
```

类型如下

```c
#define SCHED_NORMAL		0
#define SCHED_FIFO		1
#define SCHED_RR		2
#define SCHED_BATCH		3
#define SCHED_IDLE		5
#define SCHED_DEADLINE		6
```

配合调度策略的，还有我们刚才说的**优先级**，也在 task_struct 中。 

```
	int prio, static_prio, normal_prio;
	unsigned int rt_priority;
```

优先级其实就是一个数值，对于实时进程，优先级的范围是 0～99；对于普通进程，优先级的范围是 100～139。数值越小，优先级越高。从这里可以看出，所有的实时进程都比普通进程优先级要高。

### 1、实时调度策略 prio

**SCHED_FIFO、SCHED_RR、SCHED_DEADLINE**

1. **SCHED_FIFO**就是交了相同钱的，先来先服务，有的可以分配更高的优先级，也就是说，高优先级的进程可以抢占低优先级的进程，而相同优先级的进程，我们遵循先来先得。
2. **SCHED_RR 轮流调度算法**，采用时间片，相同优先级的任务当用完时间片会被放到队列尾部，以保证公平性，而高优先级的任务也是可以抢占低优先级的任务。
3. **SCHED_DEADLINE**，是按照任务的 deadline 进行调度的。当产生一个调度点的时候，DL 调度器总是选择其 deadline 距离当前时间点最近的那个任务，并调度它执行。

### 2、普通调度策略

既然大家的项目都没有那么紧急，就应该按照普通的项目流程，公平地分配人员。

**SCHED_NORMAL、SCHED_BATCH、SCHED_IDLE**

1. **SCHED_NORMAL** 是普通的进程
2. **SCHED_BATCH** 是后台进程，几乎不需要和前端进行交互。这类项目可以默默执行，不要影响需要交互的进程，可以降低他的优先级。
3. **SCHED_IDLE** 是特别空闲的时候才跑的进程，相当于咱们学习训练类的项目，比如咱们公司很长时间没有接到外在项目了，可以弄几个这样的项目练练手。



## 2、调度类

上面无论是 policy 还是 priority，都设置了一个变量，变量仅仅表示了应该这样这样干，但事情总要有人去干，谁呢？在 task_struct 里面，还有这样的成员变量：

```
const struct sched_class *sched_class;
```

**调度策略的执行逻辑，就封装在这里面，它是真正干活的那个。**

sched_class 有几种实现：

- stop_sched_class 优先级最高的任务会使用这种策略，**会中断所有其他线程，且不会被其他任务打断；**
- dl_sched_class 就对应上面的 deadline 调度策略；
- rt_sched_class 就对应 RR 算法或者 FIFO 算法的调度策略，具体调度策略由进程的 task_struct->policy 指定；
- fair_sched_class 就是普通进程的调度策略；
- idle_sched_class 就是空闲进程的调度策略。



### 完全公平调度算法 CFS	

普通进程使用的调度策略是 fair_sched_class，顾名思义，对于普通进程来讲，公平是最重要的。

CFS 全称 Completely Fair Scheduling，叫完全公平调度。

在这里得到当前的时间，以及这次的时间片开始的时间，两者相减就是这次运行的时间 delta_exec ，但是得到的这个时间其实是实际运行的时间，需要做一定的转化才作为虚拟运行时间 vruntime。转化方法如下：

​	` 虚拟运行时间 vruntime += 实际运行时间 delta_exec * NICE_0_LOAD/ 权重 `

同样的实际运行时间，给高权重的算少了，低权重的算多了，但是当选取下一个运行进程的时候，还是按照最小的 vruntime 来的，**这样高权重的获得的实际运行时间自然就多了。**相当于给一个体重 (权重)200 斤的胖子吃两个馒头，和给一个体重 100 斤的瘦子吃一个馒头，然后说，你们两个吃的是一样多。这样虽然总体胖子比瘦子多吃了一倍，但是还是公平的。

### 调度队列与调度实体

看来 CFS 需要一个数据结构来对 vruntime  进行排序，找出最小的那个。这个能够排序的数据结构不但需要查询的时候，能够快速找到最小的，更新的时候也需要能够快速的调整排序，要知道  vruntime 可是经常在变的，变了再插入这个数据结构，就需要重新排序。

能够平衡查询和更新速度的是树，在这里使用的是红黑树。

**红黑树的的节点是应该包括 vruntime 的，称为调度实体。**

对于普通进程的调度实体定义如下，这里面包含了 vruntime 和权重 load_weight，以及对于运行时间的统计。

```c
struct sched_entity {
	struct load_weight	load;		/* for load-balancing */
	struct rb_node		run_node;
	struct list_head	group_node;
	unsigned int		on_rq;

	u64			exec_start;
	u64			sum_exec_runtime;
	u64			vruntime;
	u64			prev_sum_exec_runtime;

	u64			nr_migrations;

#ifdef CONFIG_SCHEDSTATS
	struct sched_statistics statistics;
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
	int			depth;
	struct sched_entity	*parent;
	/* rq on which this entity is (to be) queued: */
	struct cfs_rq		*cfs_rq;
	/* rq "owned" by this entity/group: */
	struct cfs_rq		*my_q;
#endif

#ifdef CONFIG_SMP
	/* Per-entity load-tracking */
	struct sched_avg	avg;
#endif
};
```

类似下图

![image-20201130003254438](/home/husharp/os/os/c11/README.assets/image-20201130003254438.png)

CPU 也是这样的，每个 CPU 都有自己的 struct rq 结构，其用于描述在此 CPU 上所运行的所有进程，其包括一个**实时进程队列  rt_rq 和一个 CFS 运行队列 cfs_rq**，在调度时，调度器首先会先去实时进程队列找是否有实时进程需要运行，如果没有才会去 CFS  运行队列找是否有进行需要运行。

**每个 CPU 都有自己的 struct rq 结构:**

```c
struct rq {
	/* runqueue lock: */
	raw_spinlock_t lock;
	unsigned int nr_running;
	unsigned long cpu_load[CPU_LOAD_IDX_MAX];
......
	struct load_weight load;
	unsigned long nr_load_updates;
	u64 nr_switches;

	struct cfs_rq cfs;
	struct rt_rq rt;
	struct dl_rq dl;
......
	struct task_struct *curr, *idle, *stop;
......
};
```

对于普通进程公平队列 cfs_rq，定义如下：

```c
/* CFS-related fields in a runqueue */
struct cfs_rq {
	struct load_weight load;
	unsigned int nr_running, h_nr_running;

	u64 exec_clock;
	u64 min_vruntime;
#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;
#endif
	struct rb_root tasks_timeline;
	struct rb_node *rb_leftmost;

	struct sched_entity *curr, *next, *last, *skip;
......
};
```

这里面 rb_root 指向的就是红黑树的根节点，这个红黑树在 CPU 看起来就是一个队列，不断的取下一个应该运行的进程。rb_leftmost 指向的是最左面的节点。



### 调度类工作过程

调度类的定义如下：

```
struct sched_class {
	const struct sched_class *next;
```

next 是一个指针，指向下一个调度类。

这里我们以调度最常见的操作，**取下一个任务**为例，来解析一下。





在每个 CPU 上都有一个队列 rq，这个队列里面包含多个子队列，例如 rt_rq 和 cfs_rq，不同的队列有不同的实现方式，cfs_rq 就是用红黑树实现的。

当有一天，某个 CPU 需要找下一个任务执行的时候，会按照优先级依次调用调度类，不同的调度类操作不同的队列。当然 rt_sched_class  先被调用，它会在 rt_rq 上找下一个任务，只有找不到的时候，才轮到 fair_sched_class 被调用，它会在 cfs_rq  上找下一个任务。这样保证了实时任务的优先级永远大于普通任务。

![image-20201130004329343](/home/husharp/os/os/c11/README.assets/image-20201130004329343.png)



### 查看调度 chrt

```shell
husharp@hjh-Ubuntu:~$ chrt -p 32
pid 32 当前的调度策略︰ SCHED_OTHER
pid 32 的当前调度优先级：0
```

--->>>HuSharp OS 中的调度算法

















