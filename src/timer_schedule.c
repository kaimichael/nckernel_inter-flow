#include <assert.h>
#include <stdlib.h>

#include <sys/time.h>

#include <list.h>

#include <nckernel/timer.h>

#include "private.h"

struct nck_schedule_entry {
	struct nck_timer_entry base;

	void *context;
	nck_timer_callback callback;
	struct timeval start;

	struct list_head list;
};

EXPORT
void nck_schedule_init(struct nck_schedule *schedule)
{
	struct list_head *head = malloc(sizeof(struct list_head));
	INIT_LIST_HEAD(head);
	*schedule = (struct nck_schedule){
		.time = { 0, 0},
		.list = head,
	};
}

EXPORT
int nck_schedule_run(struct nck_schedule *schedule,
		struct timeval *next)
{
	struct list_head *head = schedule->list;
	struct nck_schedule_entry *first;

	if (list_empty(head)) {
		return 1;
	}

	first = list_first_entry(head, struct nck_schedule_entry, list);
	while (!timercmp(&schedule->time, &first->start, <)) {
		list_del_init(&first->list);
		first->callback(&first->base, first->context, 1);

		if (list_empty(head)) {
			break;
		}

		first = list_first_entry(head, struct nck_schedule_entry, list);
	}

	if (list_empty(head)) {
		timerclear(next);
	} else {
		assert(!list_empty(&first->list));
		timersub(&first->start, &schedule->time, next);
	}

	return 0;
}

EXPORT
void nck_schedule_print(FILE *file, struct nck_schedule *sched)
{
	struct nck_schedule_entry *cur;
	struct list_head *head = sched->list;

	fprintf(file, "schedule %p: time=%ld.%06ld\n", (void*)sched,
		sched->time.tv_sec, sched->time.tv_usec);

	list_for_each_entry(cur, head, list) {
		fprintf(file, "    entry %p: { start=%ld.%06ld }\n",
			(void*)cur, cur->start.tv_sec, cur->start.tv_usec);
	}
}

static int schedule_pending(struct nck_timer_entry *handle)
{
	struct nck_schedule_entry *entry = (struct nck_schedule_entry *) handle;
	// either we have a previous entry or we are at the head
	return !list_empty(&entry->list);
}

static void schedule_cancel(struct nck_timer_entry *handle)
{
	struct nck_schedule_entry *entry = (struct nck_schedule_entry *) handle;

	if (schedule_pending(handle)) {
		list_del_init(&entry->list);
		entry->callback(&entry->base, entry->context, 0);
	}
}

static void schedule_rearm(struct nck_timer_entry *handle,
		const struct timeval *delay)
{
	struct nck_schedule *schedule = (struct nck_schedule *) handle->timer->backend;
	struct nck_schedule_entry *entry = (struct nck_schedule_entry *) handle;
	struct list_head *head = schedule->list;
	struct nck_schedule_entry *insert;

	timeradd(&schedule->time, delay, &entry->start);

	// if the list is empty we can just insert and we are done
	if (list_empty(head)) {
		list_add(&entry->list, head);
		return;
	}

	// if this is already scheduled we remove it from the schedule first
	if (schedule_pending(handle)) {
		list_del_init(&entry->list);
	}

	// otherwise we iterate over the list to find the point where we need to insert
	list_for_each_entry(insert, head, list) {
		if(!timercmp(&entry->start, &insert->start, >)) {
			break;
		}
	}

	list_add_before(&entry->list, &insert->list);
}

static struct nck_timer_entry *schedule_add(struct nck_timer *timer,
		const struct timeval *delay,
		void *context,
		nck_timer_callback callback)
{
	struct nck_schedule_entry *new;

	new = malloc(sizeof(struct nck_schedule_entry));
	*new = (struct nck_schedule_entry) {
		.base = (struct nck_timer_entry) { .timer = timer },
		.context = context,
		.callback = callback,
	};
	timerclear(&new->start);
	INIT_LIST_HEAD(&new->list);

	if (delay) {
		schedule_rearm(&new->base, delay);
	}

	return &new->base;
}

static void schedule_free(struct nck_timer_entry *handle)
{
	free(handle);
}

EXPORT
void nck_schedule_timer(struct nck_schedule *schedule,
			struct nck_timer *timer)
{
	*timer = (struct nck_timer) {
		.backend = schedule,
		.add = schedule_add,
		.cancel = schedule_cancel,
		.pending = schedule_pending,
		.rearm = schedule_rearm,
		.free =	schedule_free
	};
}

EXPORT void nck_schedule_free_all(struct nck_schedule *schedule)
{
	struct nck_schedule_entry *cur, *next;
	struct list_head *head = schedule->list;

	list_for_each_entry_safe(cur, next, head, list) {
		cur->callback(&cur->base, cur->context, 0);
	}

	free(schedule->list);
}
