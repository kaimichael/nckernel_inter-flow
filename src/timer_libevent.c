#include <assert.h>
#include <stdlib.h>

#include <sys/time.h>
#include <event2/event.h>

#include <nckernel/timer.h>

#include "private.h"

struct libevent_entry {
	struct nck_timer_entry base;

	void *context;
	nck_timer_callback callback;
	struct event *ev;
};

static void libevent_trigger(int fd, short ev, void *arg)
{
	UNUSED(fd);
	UNUSED(ev);

	struct libevent_entry *entry = (struct libevent_entry *) arg;
	entry->callback(&entry->base, entry->context, 1);
}

static struct nck_timer_entry *libevent_add(struct nck_timer *timer,
		const struct timeval *delay,
		void *context,
		nck_timer_callback callback)
{
	struct libevent_entry *ret;

	ret = malloc(sizeof(*ret));
	*ret = (struct libevent_entry) {
		.base = (struct nck_timer_entry) {
			.timer = timer},
		.callback = callback,
		.context = context,
		.ev = NULL};

	ret->ev = event_new(timer->backend, -1, 0, libevent_trigger, ret);

	if (delay) {
		event_add(ret->ev, delay);
	}

	return &ret->base;
}

static void libevent_rearm(struct nck_timer_entry *handle,
		const struct timeval *delay)
{
	struct libevent_entry *timer = (struct libevent_entry *) handle;
	event_del(timer->ev);
	event_add(timer->ev, delay);
}

static void libevent_cancel(struct nck_timer_entry *handle)
{
	struct libevent_entry *timer = (struct libevent_entry *) handle;
	event_del(timer->ev);
	timer->callback(handle, timer->context, 0);
}

static int libevent_pending(struct nck_timer_entry *handle)
{
	struct libevent_entry *timer = (struct libevent_entry *) handle;
	return event_pending(timer->ev, EV_TIMEOUT, NULL);
}

static void libevent_free(struct nck_timer_entry *handle)
{
	struct libevent_entry *timer = (struct libevent_entry *) handle;
	event_free(timer->ev);
}

EXPORT
void nck_libevent_timer(struct event_base *ev, struct nck_timer *timer)
{
	*timer = (struct nck_timer) {
		.backend = ev,
		.add = libevent_add,
		.cancel = libevent_cancel,
		.pending = libevent_pending,
		.rearm = libevent_rearm,
		.free = libevent_free
	};
}
