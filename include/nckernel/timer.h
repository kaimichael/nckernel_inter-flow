#ifndef _NCK_TIMER_H_
#define _NCK_TIMER_H_

#ifdef __cplusplus
#include <ctime>
#include <cstdio>
#else
#include <time.h>
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct event_base;

/**
 * struct nck_timer_entry - Generic scheduled event.
 */
struct nck_timer_entry {
    struct nck_timer *timer;
};

typedef void (*nck_timer_callback)(struct nck_timer_entry *handle, void *context, int success);

struct nck_timer {
    void *backend;
    struct nck_timer_entry *(*add)(struct nck_timer *backend, const struct timeval *delay, void *context, nck_timer_callback callback);
    void (*cancel)(struct nck_timer_entry *handle);
    int (*pending)(struct nck_timer_entry *handle);
    void (*rearm)(struct nck_timer_entry *handle, const struct timeval *delay);
    void (*free)(struct nck_timer_entry *handle);
};

/**
 * nck_timer_add() - Create a new timer entry.
 */
struct nck_timer_entry *nck_timer_add(struct nck_timer *timer, const struct timeval *delay, void *context, nck_timer_callback callback);
/**
 * nck_timer_pending() - Check if the event is still scheduled.
 * @handle: Event to check.
 *
 * Returns: 0 if the event is not pending; 1 if it is pending.
 */
int nck_timer_pending(struct nck_timer_entry *handle);
/**
 * nck_timer_cancel() - Cancel the scheduled event.
 * @handle: Event to cancel.
 */
void nck_timer_cancel(struct nck_timer_entry *handle);
/**
 * nck_timer_rearm() - Reschedule an existing event.
 * @handle: Event to reschedule.
 * @delay: time to wait before the event will be executed.
 */
void nck_timer_rearm(struct nck_timer_entry *handle, const struct timeval *delay);
/**
 * nck_timer_free() - Free the resources used by the event entry.
 * @handle: Event to deallocate.
 */
void nck_timer_free(struct nck_timer_entry *handle);

/**
 * nck_libevent_timer() - Create a timer backed by libevent.
 * @ev: Pointer to the libevent instance.
 * @timer: The timer structure that will be initialized.
 */
void nck_libevent_timer(struct event_base *ev, struct nck_timer *timer);

/**
 * struct nck_schedule - Time table of scheduled events.
 * @time: Current time of the system, must be updated by the user.
 * @list: pointer to the list of scheduled events.
 */
struct nck_schedule {
    struct timeval time;
    void *list;
};

/**
 * nck_schedule_init() - Initialize an empty schedule.
 * @schedule: Schedule structure that will be initialized.
 */
void nck_schedule_init(struct nck_schedule *schedule);
/**
 * nck_schedule_print() - Write the currently scheduled events to a file.
 * @file: File to write to.
 * @schedule: Schedule that will be printed.
 */
void nck_schedule_print(FILE *file, struct nck_schedule *schedule);
/**
 * nck_schedule_timer() - Create a timer backed by a nck_schedule.
 * @schedule: Schedule that acts as the base of the timer.
 * @timer: Timer that will be initialized.
 */
void nck_schedule_timer(struct nck_schedule *schedule, struct nck_timer *timer);
/**
 * nck_schedule_free_all() - Free all events in the schedule.
 * @schedule: Schedule that will be freed.
 */
void nck_schedule_free_all(struct nck_schedule *schedule);
/**
 * nck_schedule_run() - Run all events up to the current time.
 * @schedule: Schedule of the events to run.
 * @next: Updated to contain the time of the next scheduled event.
 */
int nck_schedule_run(struct nck_schedule *schedule, struct timeval *next);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_TIMER_H_ */
