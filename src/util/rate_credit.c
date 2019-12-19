#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "rate.h"
#include "../private.h"

/**
 * rate_control_credit_change() - Change redundancy
 * @rc: rate_control object to change
 * @redundancy: number of repair packets to send in each phase
 */
void rate_control_credit_change(struct rate_control *rc, int32_t redundancy)
{
	assert(rc->algo == RATE_CONTROL_CREDIT);

	rc->credit.redundancy = max_t(int32_t, -rc->credit.max_symbols, redundancy);
}

static void rate_control_credit_reset_source(struct rate_control *rc)
{
	UNUSED(rc);
	/* TODO? */
}

static void rate_control_credit_reset_source_min(struct rate_control *rc)
{
	if (rc->credit.counter < rc->credit.max_symbols)
		rc->credit.counter += rc->credit.max_symbols;
}

static void rate_control_credit_reset_repair(struct rate_control *rc)
{
	if (rc->credit.counter < rc->credit.max_symbols)
		rc->credit.counter += rc->credit.max_symbols;
}

static bool rate_control_credit_next_repair(const struct rate_control *rc,
					    int source_symbols)
{
	/* source symbols will be transmitted first */
	if (source_symbols > 0)
		return false;

	/* all remaining credits will be used for repair packets */
	return rc->credit.counter >= rc->credit.max_symbols;
}

static bool rate_control_credit_has_repair(const struct rate_control *rc)
{
	return rc->credit.redundancy > 0;
}

static bool rate_control_credit_insert_allowed(const struct rate_control *rc,
					       int source_symbols)
{
	if (rc->credit.counter >= (source_symbols + 1) * rc->credit.max_symbols)
		return false;

	return true;
}

static void rate_control_credit_insert(struct rate_control *rc, bool reinsert)
{
	rc->credit.counter += rc->credit.max_symbols;

	if (!reinsert) {
		if (!rc->credit.advance_use) {
			/* if the coder doesn't use advance, use the old behaviour */
			rc->credit.counter += rc->credit.redundancy;
		} else {
			int32_t advance_credit;

			advance_credit = rc->credit.advance_credit;
			/* do not actually decrease the credit counter */
			advance_credit = min_t(int32_t, advance_credit, rc->credit.max_symbols);
			/* don't send more than 5 packets per incoming packet.
			 * this may need to be verified in practice ... */
			advance_credit = max_t(int32_t, advance_credit, -rc->credit.max_symbols * 5);

			rc->credit.counter -= advance_credit;
			rc->credit.advance_credit -= advance_credit;

			rc->credit.advance_credit += rc->credit.max_symbols;
		}
	}
}

static void rate_control_credit_advance(struct rate_control *rc, int advance)
{
	rc->credit.advance_use = true;

	if (advance < 0)
		return;

	rc->credit.advance_credit -= (rc->credit.max_symbols + rc->credit.redundancy) * advance;

	/* cap it */
	rc->credit.advance_credit = max_t(int32_t, rc->credit.advance_credit,
					     -rc->credit.max_symbols * rc->credit.max_symbols);
}

static void rate_control_credit_flush(struct rate_control *rc)
{
	int diff;

	if (!rc->credit.advance_use)
		return;

	/* make sure that the next symbol is immediately sent, and reset
	 * the advance credit */
	diff = rc->credit.max_symbols - rc->credit.counter - 1;
	if (diff < 0)
		return;

	rc->credit.counter += diff;
	rc->credit.advance_credit = rc->credit.max_symbols - 1;
}

static bool rate_control_credit_step(struct rate_control *rc,
				     int source_symbols)
{
	bool repair = !!rate_control_credit_next_repair(rc, source_symbols);

	rc->credit.counter -= rc->credit.max_symbols;

	return repair;
}

/**
 * rate_control_credit_init() - Initialize rate control object to start with source phase
 * @rc: rate_control object to initialize
 * @symbols: number of packets in window
 * @redundancy: number of repair packets to send in each phase
 */
void rate_control_credit_init(struct rate_control *rc, uint32_t symbols,
			      int32_t redundancy)
{
	rc->algo = RATE_CONTROL_CREDIT;

	rc->credit.max_symbols = symbols;
	rc->credit.redundancy = max_t(int32_t, -symbols, redundancy);
	rc->credit.counter = 0;
	rc->credit.advance_credit = 0;
	rc->credit.advance_use = false;

	/* function pointers */
	rc->reset_source = rate_control_credit_reset_source;
	rc->reset_source_min = rate_control_credit_reset_source_min;
	rc->reset_repair = rate_control_credit_reset_repair;
	rc->next_repair = rate_control_credit_next_repair;
	rc->has_repair = rate_control_credit_has_repair;
	rc->insert_allowed = rate_control_credit_insert_allowed;
	rc->insert = rate_control_credit_insert;
	rc->advance = rate_control_credit_advance;
	rc->flush = rate_control_credit_flush;
	rc->step = rate_control_credit_step;
}
