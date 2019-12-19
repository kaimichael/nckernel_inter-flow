#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * enum rate_control_algo - selected rate control algorithm
 * @RATE_CONTROL_DUAL: dual phase with explicit source/coded phase
 */
enum rate_control_algo {
	RATE_CONTROL_DUAL,
	RATE_CONTROL_CREDIT,
};

/**
 * struct rate_control_dual - dual phase rate control structure
 * @source_phase: number of source packets to send in each phase
 * @repair_phase: number of repair packets to send in each phase
 * @source_counter: number of source packets which can still be sent in current
 *  source phase
 * @repair_counter: number of repair packets which can still be sent in current
 *  repair phase
 */
struct rate_control_dual {
	int source_phase;
	int repair_phase;
	int source_counter;
	int repair_counter;
};

/**
 * struct rate_control_credit - credit based rate control structure
 * @max_symbols: maximum number of packets in window
 * @redundancy: number of repair packets to send in each phase
 * @counter: currently allocated credits
 * @advance_credit: allowance to apply negative redundancy
 */
struct rate_control_credit {
	uint32_t max_symbols;
	int32_t redundancy;
	uint32_t counter;
	int32_t advance_credit;
	bool advance_use;
};

/**
 * struct rate_control - general rate control structure
 * @dual: dual phase specific variables
 * @algo: currently selected algo
 */
struct rate_control {
	union {
		struct rate_control_dual dual;
		struct rate_control_credit credit;
	};
	enum rate_control_algo algo;

	/* function pointers */
	void (*reset_source)(struct rate_control *rc);
	void (*reset_source_min)(struct rate_control *rc);
	void (*reset_repair)(struct rate_control *rc);
	bool (*next_repair)(const struct rate_control *rc, int source_symbols);
	bool (*has_repair)(const struct rate_control *rc);
	bool (*insert_allowed)(const struct rate_control *rc, int source_symbols);
	void (*insert)(struct rate_control *rc, bool reinsert);
	void (*advance)(struct rate_control *rc, int advance);
	void (*flush)(struct rate_control *rc);
	bool (*step)(struct rate_control *rc, int source_symbols);
};

/* dual phase specific */
void rate_control_dual_init(struct rate_control *rc, int source_phase, int repair_phase);
void rate_control_dual_change(struct rate_control *rc, int source_phase, int repair_phase);
void rate_control_dual_reset(struct rate_control *rc, bool repair, int packets);

/* credit specific */
void rate_control_credit_init(struct rate_control *rc, uint32_t symbols, int32_t redundancy);
void rate_control_credit_change(struct rate_control *rc, int32_t redundancy);

/* shared function wrapper */

/**
 * rate_control_reset_source() - Explicitely switch to next source phase
 * @rc: rate_control object to reset
 */
static __inline__ void rate_control_reset_source(struct rate_control *rc)
{
	rc->reset_source(rc);
}

/**
 * rate_control_reset_source_min() - Explicitely switch to short source phase
 * @rc: rate_control object to reset
 *
 * This makes sure that the rate control will at least send a single source
 * packet
 */
static __inline__ void rate_control_reset_source_min(struct rate_control *rc)
{
	rc->reset_source_min(rc);
}

/**
 * rate_control_reset_repair() - Explicitely switch to next repair phase
 * @rc: rate_control object to reset
 */
static __inline__ void rate_control_reset_repair(struct rate_control *rc)
{
	rc->reset_repair(rc);
}

/**
 * rate_control_next_repair() - Check if next packet will be a repair packet
 * @rc: rate_control object to check
 * @source_symbols: number of currently queued up source symbols
 *
 * Return: true when next packet should be a repair packet, otherwise false
 */
static __inline__ bool rate_control_next_repair(const struct rate_control *rc,
						int source_symbols)
{
	return rc->next_repair(rc, source_symbols);
}

/**
 * rate_control_has_repair() - Check if rate control will create repair packets
 * @rc: rate_control object to check
 *
 * Return: true when repair phase exists, otherwise false
 */
static __inline__ bool rate_control_has_repair(const struct rate_control *rc)
{
	return rc->has_repair(rc);
}

/**
 * rate_control_insert_allowed() - Check if new symbols can currently accepted
 * @rc: rate_control object to check
 * @source_symbols: number of currently queued up source symbols
 *
 * Return: true when rate control allows new symbol, otherwise false
 */
static __inline__ bool rate_control_insert_allowed(const struct rate_control *rc,
						   int source_symbols)
{
	return rc->insert_allowed(rc, source_symbols);
}

/**
 * rate_control_insert() - Account for inserted symbol
 * @rc: rate_control object to check
 * @reinsert: whether the symbol is re-inserted (true) or completely new (false)
 */
static __inline__ void rate_control_insert(struct rate_control *rc, bool reinsert)
{
	rc->insert(rc, reinsert);
}

/**
 * rate_control_advance() - Account for advancing symbols in the window or generation
 * @rc: rate_control object to check
 * @reinsert: by how many symbols we have advanced compared to before
 *
 * Use this if the configured redundancy rate is meant per advanced symbol (e.g.
 * in a sliding window) and not per received innovative packet. If the latter functionality
 * is intended, just do not use rate_control_advance().
 */
static __inline__ void rate_control_advance(struct rate_control *rc, int advance)
{
	rc->advance(rc, advance);
}

/**
 * rate_control_flush() - Flush/finish a transmission phase
 * @rc: rate_control object to check
 * @reinsert: by how many symbols we have advanced compared to before
 *
 * This function is called when a flush is called in the protocol. The rate controller
 * can then finish the current session (e.g. by sending another packet) and prepare for
 * the next session (e.g. by making sure that the first packet will definitely be
 * sent).
 */
static __inline__ void rate_control_flush(struct rate_control *rc)
{
	rc->flush(rc);
}

/**
 * rate_control_step() - Take on packet and return type of it
 * @rc: rate_control object to modify
 * @source_symbols: number of currently queued up source symbols
 *
 * Return: true when "stepped" packet should be a repair packet, otherwise false
 */
static __inline__ bool rate_control_step(struct rate_control *rc, int source_symbols)
{
	return rc->step(rc, source_symbols);
}

#ifdef __cplusplus
}
#endif
