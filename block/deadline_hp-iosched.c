/*
 *  deadline_hp - read-priority IO scheduler for gaming workloads.
 *
 *  Based on Jens Axboe's deadline elevator (single-queue interface),
 *  retuned for game workloads where reads dominate and latency matters.
 *
 *  Design goals vs. stock deadline:
 *    - Reads always win dispatch priority while a read backlog exists.
 *    - A read request is dispatched within ~10ms of arrival (low read_expire).
 *    - Sequential reads are batched (large fifo_batch) for UFS throughput.
 *    - Front merges are intentionally DISABLED: UFS presents deep,
 *      parallel hardware queues, so merging adjacent requests serializes
 *      work that the device could process in parallel, and the rbtree
 *      churn (del+reinsert under queue lock) costs CPU for no gain.
 *    - Writes are NOT starved forever: after `writes_starved` read batches,
 *      one write batch is served. This keeps journal/fsync from being
 *      postponed indefinitely (the original "read absolute priority" hack
 *      removed this and risked filesystem hangs under sustained reads).
 *
 *  Copyright (C) 2002 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>

/*
 * Tunables (game-tuned defaults)
 *   read_expire : very short -> reads never wait long.
 *   write_expire: kept short-ish; writes mainly gated by writes_starved.
 *   writes_starved: generous -> reads keep priority, but writes DO get
 *                   a turn eventually (no permanent starvation).
 *   fifo_batch  : large -> sequential read streams batched for throughput.
 */
static const int read_expire = (HZ * 10) / 1000;  /* ~10ms */
static const int write_expire = 3 * HZ;               /* ~1s  */
static const int writes_starved = 64;             /* read batches before a write batch */
static const int fifo_batch = 256;                /* sequential requests per batch */

struct deadline_data {
	struct rb_root sort_list[2];
	struct list_head fifo_list[2];

	struct request *next_rq[2];
	unsigned int batching;		/* number of sequential requests made */
	unsigned int starved;		/* times reads have starved writes */

	/* tunables */
	int fifo_expire[2];
	int fifo_batch;
	int writes_starved;
};

static inline struct rb_root *
deadline_rb_root(struct deadline_data *dd, struct request *rq)
{
	return &dd->sort_list[rq_data_dir(rq)];
}

static inline struct request *
deadline_latter_request(struct request *rq)
{
	struct rb_node *node = rb_next(&rq->rb_node);

	if (node)
		return rb_entry_rq(node);

	return NULL;
}

static void
deadline_add_rq_rb(struct deadline_data *dd, struct request *rq)
{
	elv_rb_add(deadline_rb_root(dd, rq), rq);
}

static inline void
deadline_del_rq_rb(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	if (dd->next_rq[data_dir] == rq)
		dd->next_rq[data_dir] = deadline_latter_request(rq);

	elv_rb_del(deadline_rb_root(dd, rq), rq);
}

static void
deadline_add_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const int data_dir = rq_data_dir(rq);

	blk_req_zone_write_unlock(rq);

	deadline_add_rq_rb(dd, rq);

	rq->fifo_time = jiffies + dd->fifo_expire[data_dir];
	list_add_tail(&rq->queuelist, &dd->fifo_list[data_dir]);
}

static void deadline_remove_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	rq_fifo_clear(rq);
	deadline_del_rq_rb(dd, rq);
}

/*
 * No scheduler-level merging. UFS has deep parallel hardware queues;
 * front merges would serialize independent requests and cost rbtree
 * churn under the queue lock. Back merges still happen in the block
 * layer's generic path (plug list), so nothing is lost for sequential
 * streams.
 */
static enum elv_merge
deadline_merge(struct request_queue *q, struct request **req, struct bio *bio)
{
	return ELEVATOR_NO_MERGE;
}

static void
deadline_merged_requests(struct request_queue *q, struct request *req,
			 struct request *next)
{
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before((unsigned long)next->fifo_time,
				(unsigned long)req->fifo_time)) {
			list_move(&req->queuelist, &next->queuelist);
			req->fifo_time = next->fifo_time;
		}
	}

	deadline_remove_request(q, next);
}

static inline void
deadline_move_to_dispatch(struct deadline_data *dd, struct request *rq)
{
	blk_req_zone_write_lock(rq);

	deadline_remove_request(rq->q, rq);
	elv_dispatch_add_tail(rq->q, rq);
}

static void
deadline_move_request(struct deadline_data *dd, struct request *rq)
{
	const int data_dir = rq_data_dir(rq);

	dd->next_rq[READ] = NULL;
	dd->next_rq[WRITE] = NULL;
	dd->next_rq[data_dir] = deadline_latter_request(rq);

	deadline_move_to_dispatch(dd, rq);
}

static inline int deadline_check_fifo(struct deadline_data *dd, int ddir)
{
	struct request *rq = rq_entry_fifo(dd->fifo_list[ddir].next);

	return time_after_eq(jiffies, (unsigned long)rq->fifo_time);
}

static struct request *
deadline_fifo_request(struct deadline_data *dd, int data_dir)
{
	struct request *rq;

	if (WARN_ON_ONCE(data_dir != READ && data_dir != WRITE))
		return NULL;

	if (list_empty(&dd->fifo_list[data_dir]))
		return NULL;

	rq = rq_entry_fifo(dd->fifo_list[data_dir].next);
	if (data_dir == READ || !blk_queue_is_zoned(rq->q))
		return rq;

	list_for_each_entry(rq, &dd->fifo_list[WRITE], queuelist)
		if (blk_req_can_dispatch_to_zone(rq))
			return rq;

	return NULL;
}

static struct request *
deadline_next_request(struct deadline_data *dd, int data_dir)
{
	struct request *rq;

	if (WARN_ON_ONCE(data_dir != READ && data_dir != WRITE))
		return NULL;

	rq = dd->next_rq[data_dir];
	if (!rq)
		return NULL;

	if (data_dir == READ || !blk_queue_is_zoned(rq->q))
		return rq;

	while (rq) {
		if (blk_req_can_dispatch_to_zone(rq))
			return rq;
		rq = deadline_latter_request(rq);
	}

	return NULL;
}

/*
 * deadline_dispatch_requests: read-priority arbitration with a write
 * starvation guard. Reads keep priority while any read is queued, but a
 * write batch is served after `writes_starved` consecutive read batches so
 * background writes are not postponed forever.
 */
static int deadline_dispatch_requests(struct request_queue *q, int force)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const int reads = !list_empty(&dd->fifo_list[READ]);
	const int writes = !list_empty(&dd->fifo_list[WRITE]);
	struct request *rq, *next_rq;
	int data_dir;

	/* continue an in-progress batch */
	rq = deadline_next_request(dd, WRITE);
	if (!rq)
		rq = deadline_next_request(dd, READ);

	if (rq && dd->batching < dd->fifo_batch)
		goto dispatch_request;

	/* start a new batch: pick direction */
	if (reads) {
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[READ]));

		/* read priority, with a write starvation guard */
		if (writes && dd->starved >= dd->writes_starved) {
			dd->starved = 0;
			data_dir = WRITE;
			goto dispatch_find_request;
		}

		dd->starved++;
		data_dir = READ;
		goto dispatch_find_request;
	}

	if (writes) {
		BUG_ON(RB_EMPTY_ROOT(&dd->sort_list[WRITE]));

		dd->starved = 0;
		data_dir = WRITE;
		goto dispatch_find_request;
	}

	return 0;

dispatch_find_request:
	next_rq = deadline_next_request(dd, data_dir);
	if (deadline_check_fifo(dd, data_dir) || !next_rq)
		rq = deadline_fifo_request(dd, data_dir);
	else
		rq = next_rq;

	if (!rq)
		return 0;

	dd->batching = 0;

dispatch_request:
	dd->batching++;
	deadline_move_request(dd, rq);

	return 1;
}

static void
deadline_completed_request(struct request_queue *q, struct request *rq)
{
	blk_req_zone_write_unlock(rq);
}

static void deadline_exit_queue(struct elevator_queue *e)
{
	struct deadline_data *dd = e->elevator_data;

	BUG_ON(!list_empty(&dd->fifo_list[READ]));
	BUG_ON(!list_empty(&dd->fifo_list[WRITE]));

	kfree(dd);
}

static int deadline_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct deadline_data *dd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	dd = kzalloc_node(sizeof(*dd), GFP_KERNEL, q->node);
	if (!dd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = dd;

	INIT_LIST_HEAD(&dd->fifo_list[READ]);
	INIT_LIST_HEAD(&dd->fifo_list[WRITE]);
	dd->sort_list[READ] = RB_ROOT;
	dd->sort_list[WRITE] = RB_ROOT;
	dd->fifo_expire[READ] = read_expire;
	dd->fifo_expire[WRITE] = write_expire;
	dd->writes_starved = writes_starved;
	dd->fifo_batch = fifo_batch;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

/*
 * sysfs
 */
static ssize_t
deadline_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static void
deadline_var_store(int *var, const char *page)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data = __VAR;						\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return deadline_var_show(__data, (page));			\
}
SHOW_FUNCTION(deadline_read_expire_show, dd->fifo_expire[READ], 1);
SHOW_FUNCTION(deadline_write_expire_show, dd->fifo_expire[WRITE], 1);
SHOW_FUNCTION(deadline_writes_starved_show, dd->writes_starved, 0);
SHOW_FUNCTION(deadline_fifo_batch_show, dd->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data;							\
	deadline_var_store(&__data, (page));				\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return count;							\
}
STORE_FUNCTION(deadline_read_expire_store, &dd->fifo_expire[READ], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_write_expire_store, &dd->fifo_expire[WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_writes_starved_store, &dd->writes_starved, INT_MIN, INT_MAX, 0);
STORE_FUNCTION(deadline_fifo_batch_store, &dd->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
	__ATTR(name, 0644, deadline_##name##_show, deadline_##name##_store)

static struct elv_fs_entry deadline_attrs[] = {
	DD_ATTR(read_expire),
	DD_ATTR(write_expire),
	DD_ATTR(writes_starved),
	DD_ATTR(fifo_batch),
	__ATTR_NULL
};

static struct elevator_type iosched_deadline = {
	.ops.sq = {
		.elevator_merge_fn =		deadline_merge,
		.elevator_merge_req_fn =	deadline_merged_requests,
		.elevator_dispatch_fn =		deadline_dispatch_requests,
		.elevator_completed_req_fn =	deadline_completed_request,
		.elevator_add_req_fn =		deadline_add_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_init_fn =		deadline_init_queue,
		.elevator_exit_fn =		deadline_exit_queue,
	},

	.elevator_attrs = deadline_attrs,
	.elevator_name = "deadline_hp",
	.elevator_owner = THIS_MODULE,
};

static int __init deadline_init(void)
{
	return elv_register(&iosched_deadline);
}

static void __exit deadline_exit(void)
{
	elv_unregister(&iosched_deadline);
}

module_init(deadline_init);
module_exit(deadline_exit);

MODULE_AUTHOR("Jens Axboe (retuned for gaming by user)");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("deadline IO scheduler - read-priority gaming variant");
