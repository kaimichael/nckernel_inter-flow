#include <stdlib.h>

#include <nckernel/nckernel.h>
#include <nckernel/api.h>

#include "private.h"

void nck_trigger_init(struct nck_trigger *trigger)
{
	trigger->context = NULL;
	trigger->callback = NULL;
}

void nck_trigger_set(struct nck_trigger *trigger,
		     void *context, void (*callback)(void *context))
{
	trigger->context = context;
	trigger->callback = callback;
}

void nck_trigger_call(struct nck_trigger *trigger)
{
	if (trigger->callback) {
		trigger->callback(trigger->context);
	}
}

