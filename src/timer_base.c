#include <assert.h>
#include <stdlib.h>

#include <sys/time.h>

#include <nckernel/timer.h>

#include "private.h"

EXPORT
struct nck_timer_entry *nck_timer_add(struct nck_timer *timer,
		const struct timeval *delay,
		void *context,
		nck_timer_callback callback)
{
	struct nck_timer_entry *entry;
	if (!timer) {
		return NULL;
	}

	assert(timer->add != NULL);

	entry = timer->add(timer, delay, context, callback);
	assert(entry->timer == timer);

	return entry;
}

EXPORT
void nck_timer_cancel(struct nck_timer_entry *handle)
{
	if (handle) {
		assert(handle->timer != NULL);
		assert(handle->timer->cancel != NULL);
		handle->timer->cancel(handle);
	}
}

EXPORT
int nck_timer_pending(struct nck_timer_entry *handle)
{
	if (!handle) {
		return 0;
	}

	assert(handle->timer != NULL);
	assert(handle->timer->pending != NULL);
	return handle->timer->pending(handle);
}

EXPORT
void nck_timer_rearm(struct nck_timer_entry *handle,
		const struct timeval *delay)
{
	if (handle) {
		assert(handle->timer != NULL);
		assert(handle->timer->rearm != NULL);
		handle->timer->rearm(handle, delay);
	}
}

EXPORT
void nck_timer_free(struct nck_timer_entry *handle)
{
	if (handle) {
		assert(handle->timer != NULL);
		assert(handle->timer->free != NULL);
		handle->timer->free(handle);
	}
}
