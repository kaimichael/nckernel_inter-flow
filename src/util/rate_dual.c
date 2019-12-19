#include <assert.h>

#include "rate.h"
#include "../private.h"

/**
 * rate_control_dual_change() - Change phase sizes and adjusts counters for it
 * @rc: rate_control object to change
 * @source_phase: number of source packets to send in each phase
 * @repair_phase: number of repair packets to send in each phase
 */
void rate_control_dual_change(struct rate_control *rc, int source_phase,
			      int repair_phase)
{
	assert(rc->algo == RATE_CONTROL_DUAL);
	assert(rc->dual.source_phase > 0);

	if (rate_control_next_repair(rc, 0)) {
		rc->dual.repair_counter += repair_phase - rc->dual.repair_phase;
		if (rc->dual.repair_counter <= 0) {
			rc->dual.repair_counter = 1;
		}
	} else {
		rc->dual.source_counter += source_phase - rc->dual.source_phase;
		if (rc->dual.source_counter <= 0) {
			rc->dual.source_counter = 1;
		}
	}

	rc->dual.source_phase = source_phase;
	rc->dual.repair_phase = repair_phase;
}

/**
 * rate_control_dual_reset() - Explicitely switch to source/repair phase
 * @rc: rate_control object to reset
 * @repair: whether to start repair phase
 * @packets: number of packets to send in this phase
 */
void rate_control_dual_reset(struct rate_control *rc, bool repair, int packets)
{
	if (repair) {
		assert(rc->dual.repair_phase > 0);
		rc->dual.source_counter = 0;
		rc->dual.repair_counter = packets;
	} else {
		rc->dual.source_counter = packets;
		rc->dual.repair_counter = 0;
	}
}

static void rate_control_dual_reset_source(struct rate_control *rc)
{
	rate_control_dual_reset(rc, 0, rc->dual.source_phase);
}

static void rate_control_dual_reset_source_min(struct rate_control *rc)
{
	rate_control_dual_reset(rc, 0, 1);
}

static void rate_control_dual_reset_repair(struct rate_control *rc)
{
	assert(rc->dual.repair_phase > 0);

	rate_control_dual_reset(rc, 1, rc->dual.repair_phase);
}

static bool rate_control_dual_next_repair(const struct rate_control *rc,
					  int source_symbols)
{
	UNUSED(source_symbols);

	return rc->dual.source_counter == 0;
}

static bool rate_control_dual_has_repair(const struct rate_control *rc)
{
	return rc->dual.repair_phase > 0;
}

static bool rate_control_dual_insert_allowed(const struct rate_control *rc,
					     int source_symbols)
{
	if (source_symbols >= rc->dual.source_counter)
		return false;

	return true;
}

static void rate_control_dual_insert(struct rate_control *rc, bool reinsert)
{
	/* TODO? */
	UNUSED(rc);
	UNUSED(reinsert);
}

static void rate_control_dual_advance(struct rate_control *rc, int advance)
{
	/* TODO? */
	UNUSED(rc);
	UNUSED(advance);
}

static void rate_control_dual_flush(struct rate_control *rc)
{
	/* TODO? */
	UNUSED(rc);
}

static bool rate_control_dual_step(struct rate_control *rc,
				   int source_symbols)
{
	bool repair = !!rate_control_dual_next_repair(rc, source_symbols);

	if (repair) {
		rc->dual.repair_counter--;
		if (rc->dual.repair_counter == 0) {
			rc->dual.source_counter = rc->dual.source_phase;
		}
	} else if(rc->dual.repair_phase > 0) {
		rc->dual.source_counter--;
		if (rc->dual.source_counter == 0 ) {
			rc->dual.repair_counter = rc->dual.repair_phase;
		}
	}

	return repair;
}

/**
 * rate_control_dual_init() - Initialize rate control object to start with source phase
 * @rc: rate_control object to initialize
 * @source_phase: number of source packets to send in each phase
 * @repair_phase: number of repair packets to send in each phase
 */
void rate_control_dual_init(struct rate_control *rc, int source_phase, int repair_phase)
{
	assert(source_phase > 0);

	rc->algo = RATE_CONTROL_DUAL;

	rc->dual.source_phase = source_phase;
	rc->dual.repair_phase = repair_phase;

	rc->dual.source_counter = source_phase;
	rc->dual.repair_counter = 0;

	/* function pointers */
	rc->reset_source = rate_control_dual_reset_source;
	rc->reset_source_min = rate_control_dual_reset_source_min;
	rc->reset_repair = rate_control_dual_reset_repair;
	rc->next_repair = rate_control_dual_next_repair;
	rc->has_repair = rate_control_dual_has_repair;
	rc->insert_allowed = rate_control_dual_insert_allowed;
	rc->insert = rate_control_dual_insert;
	rc->advance = rate_control_dual_advance;
	rc->flush = rate_control_dual_flush;
	rc->step = rate_control_dual_step;
}
