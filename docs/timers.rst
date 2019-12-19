Timers
======

Some protocols can use timing information. For example a retransmission can be
triggered when an acknowledgement is not received after a specified timeout.
NCKernel can be used in many different contexts and needs a flexible and
abstract mechanism to implement timing. For this we added the timers API.

Currently we include two implementations of timers. One implementation uses
``libevent`` to trigger the timers. This is recommended for real-world
programs. The other implementation uses a custom scheduler with an arbitrary
time source. This is appropriate for simulations which usually need a simulated
clock. But it is lso possible to provide a custom implementation, for example
to integrate NCKernel in other language runtimes.

Scheduling an Event
-------------------

The main use of timers is to schedule events that will be triggered in a
specific time. This can be used for example for a retransmission timeout in a
protocol. It is recommended to create a :c:type:`nck_timer_entry` for each
required event with the :c:func:`nck_timer_add` function and reuse it with the
:c:func:`nck_timer_rearm` function.

Simulating Time
---------------

It is recommended to use ``libevent`` for real applications. But for simulations,
time needs to be simulated. The :c:type:`nck_schedule` can be used for this purpose.
It provides a set of functions to use an arbitrary time source and execute events
based on this. The following example shows the basic usage of this data structure.
The main steps are to call :c:func:`nck_schedule_run` to execute all scheduled events and
update :c:member:`nck_schedule.time` value.

.. code:: c

    struct nck_schedule sched;
    struct nck_timer timer;
    struct timeval step;

    nck_schedule_init(&sched);
    nck_schedule_timer(&sched, &timer);

    // add some scheduled events, like generating source packets

    // run until nothing is scheduled
    while (!nck_schedule_run(&sched, &step)) {
        // some processing ...

        // increment the simulated time
        timeradd(&sched.time, &step, &sched.time);
    }

API
---

.. kernel-doc:: include/nckernel/timer.h
