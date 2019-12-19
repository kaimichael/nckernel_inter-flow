#include <cutest.h>
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>

#include <rep/sarq.h>

#define TEST_ASSERT(cond) assert(TEST_CHECK(cond))
#define TEST_ASSERT_(cond, ...) assert(TEST_CHECK_(cond, __VA_ARGS__))

/** test_offset() - Verify the bound checks. */
void test_offset()
{
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));
	sarq_set_offset(&sarq, 0);

	// basic offset functions
	TEST_ASSERT(!sarq_before(&sarq, 1));
	TEST_ASSERT(sarq_after(&sarq, 1));

	TEST_ASSERT(!sarq_before(&sarq, 0));
	TEST_ASSERT(!sarq_after(&sarq, 0));

	TEST_ASSERT(!sarq_before(&sarq, (unsigned)(-size+1)));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)(-size+1)));

	TEST_ASSERT(sarq_before(&sarq, (unsigned)-size));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)-size));

	// test with shifted window
	sarq_set_offset(&sarq, 2);

	TEST_ASSERT(!sarq_before(&sarq, 3));
	TEST_ASSERT(sarq_after(&sarq, 3));

	TEST_ASSERT(!sarq_before(&sarq, 2));
	TEST_ASSERT(!sarq_after(&sarq, 2));

	TEST_ASSERT(!sarq_before(&sarq, (unsigned)(2-size+1)));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)(2-size+1)));

	TEST_ASSERT(sarq_before(&sarq, (unsigned)(2-size)));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)(2-size)));

	// test with wrapped window
	sarq_set_offset(&sarq, (unsigned)-4);

	TEST_ASSERT(!sarq_before(&sarq, -3));
	TEST_ASSERT(sarq_after(&sarq, -3));

	TEST_ASSERT(!sarq_before(&sarq, -4));
	TEST_ASSERT(!sarq_after(&sarq, -4));

	TEST_ASSERT(!sarq_before(&sarq, (unsigned)(-4-size+1)));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)(-4-size+1)));

	TEST_ASSERT(sarq_before(&sarq, (unsigned)(-4-size)));
	TEST_ASSERT(!sarq_after(&sarq, (unsigned)(-4-size)));
}

/** test_take() - Test a simple packet transmission. */
void test_take()
{
	unsigned no = 0;
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	TEST_ASSERT(sarq_add(&sarq, 1) == 0);

	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(no == 1);

	// a next should not change the state
	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(no == 1);

	// after a take the number should be gone
	TEST_ASSERT(sarq_take(&sarq, no, 1) == 0);
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_ack() - Test simple acknowledgements. */
void test_ack()
{
	unsigned no = 0;
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	TEST_ASSERT(sarq_add(&sarq, 1) == 0);

	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(no == 1);
	TEST_ASSERT(sarq_take(&sarq, no, 1) == 0);

	TEST_ASSERT(sarq_retransmit(&sarq, 1) == 1);
	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(sarq_ack(&sarq, no) == 0);
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_bulkack() - Test bulk acknowledgements. */
void test_bulkack()
{
	struct sarq sarq;

	unsigned no = 0, i;
	unsigned size = 8;
	unsigned count = 6;
	unsigned pktno = 1;

	char ack_no = 1;
	unsigned ack_count = 4;
	unsigned char ack_mask[] = { 0xa0 };
	unsigned retx_nos[] = { 2, 4, 5 };

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	// add all packets for sending
	for (i = 0; i < count; ++i) {
		TEST_ASSERT_(sarq_add(&sarq, i) == 0,
				"Add packet %d", i);
	}

	// simulate sending the packets
	for (i = 0; i < count; ++i) {
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == i,
				"Next transmission expected: %d actual: %d", i, no);
		TEST_ASSERT_(sarq_take(&sarq, no, pktno++) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	// nothing should be available right now
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);

	// do the bulk ack
	TEST_ASSERT(sarq_bulk_ack(&sarq, ack_no, ack_count, ack_mask) == 0);
	TEST_ASSERT(sarq_retransmit(&sarq, pktno) == sizeof(retx_nos) / sizeof(*retx_nos));

	// check if we retransmit the correct numbers
	for (i = 0; i < sizeof(retx_nos) / sizeof(*retx_nos); ++i) {
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == retx_nos[i],
				"Next transmission expected: %d actual: %d", retx_nos[i], no);
		TEST_ASSERT_(sarq_take(&sarq, no, pktno++) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_bulkack_partial() - Test bulk acknowledgements where part of the bulk is outdated. */
void test_bulkack_partial()
{
	struct sarq sarq;

	unsigned no, i;
	unsigned size = 8;
	unsigned count = 10;
	unsigned pktno = 1;

	char ack_no = 1;
	unsigned ack_count = 6;
	unsigned char ack_mask[] = { 0xa8 };
	unsigned retx_nos[] = { 2, 4, 6, 7, 8 };

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	// add all packets for sending
	for (i = 0; i < count; ++i) {
		TEST_ASSERT_(sarq_add(&sarq, i) == 0,
				"Add packet %d", i);
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == i,
				"Next transmission expected: %d actual: %d", i, no);
		TEST_ASSERT_(sarq_take(&sarq, no, pktno++) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	// nothing should be available right now
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);

	// do the bulk ack
	TEST_ASSERT(sarq_bulk_ack(&sarq, ack_no, ack_count, ack_mask) == 0);
	TEST_ASSERT(sarq_retransmit(&sarq, 9) == sizeof(retx_nos) / sizeof(*retx_nos));

	// check if we retransmit the correct numbers
	for (i = 0; i < sizeof(retx_nos) / sizeof(*retx_nos); ++i) {
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == retx_nos[i],
				"Next transmission expected: %d actual: %d", retx_nos[i], no);
		TEST_ASSERT_(sarq_take(&sarq, no, pktno) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_bulkack_ahead() - Test bulk acknowledgements where part of the bulk is ahead of the current offset. */
void test_bulkack_ahead()
{
	struct sarq sarq;

	unsigned no = 0, i;
	unsigned size = 8;
	unsigned count = 6;

	char ack_no = 4;
	unsigned ack_count = 4;
	unsigned char ack_mask[] = { 0xd0 };
	unsigned retx_nos[] = { 6 };

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	// add all packets for sending
	for (i = 0; i < count; ++i) {
		TEST_ASSERT_(sarq_add(&sarq, i) == 0,
				"Add packet %d", i);
	}

	// simulate sending the packets
	for (i = 0; i < count; ++i) {
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == i,
				"Next transmission expected: %d actual: %d", i, no);
		TEST_ASSERT_(sarq_take(&sarq, no, 1) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	// nothing should be available right now
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);

	// do the bulk ack
	TEST_ASSERT(sarq_bulk_ack(&sarq, ack_no, ack_count, ack_mask) == 0);
	TEST_ASSERT(sarq_retransmit(&sarq, ack_no) == 0);

	// here we should have nothing to retransmit
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);

	// now we add the missing packets
	for (i = 0; i < ack_count; ++i) {
		sarq_add(&sarq, ack_no + i);
	}

	// check if we retransmit the correct numbers
	for (i = 0; i < sizeof(retx_nos) / sizeof(*retx_nos); ++i) {
		TEST_ASSERT_(sarq_next(&sarq, &no) == 0,
				"Get transmission %d", i);
		TEST_ASSERT_(no == retx_nos[i],
				"Next transmission expected: %d actual: %d", retx_nos[i], no);
		TEST_ASSERT_(sarq_take(&sarq, no, 2) == 0,
				"Transmission %d (no=%d)", i, no);
	}

	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_bitmask() - Test writing a bitmask of acknowledgements. */
void test_bitmask()
{
	unsigned i, no;
	unsigned size = 8;
	struct sarq sarq;

	unsigned acks[] = { 0, 1, 3, 5, 7 };

	unsigned count;
	unsigned char mask[1];

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	for (i = 0; i < sizeof(acks)/sizeof(*acks); ++i) {
		TEST_ASSERT_(sarq_ack(&sarq, acks[i]) == 0,
				"ack %d", i);
	}

	count = sarq_ack_mask(&sarq, &no, sizeof(mask), mask);
	TEST_ASSERT(no == 2);
	TEST_ASSERT(count == 6);
	TEST_ASSERT(mask[0] = 0xa4);
}

/** test_bitmask_empty() - Test writing a bitmask if everything is acknowledged. */
void test_bitmask_empty()
{
	unsigned i, no;
	unsigned size = 8;
	struct sarq sarq;

	unsigned count;
	unsigned char mask[1];

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	for (i = 0; i < size; ++i) {
		TEST_ASSERT_(sarq_ack(&sarq, i) == 0,
				"ack %d", i);
	}

	count = sarq_ack_mask(&sarq, &no, sizeof(mask), mask);
	TEST_ASSERT(no == 7);
	TEST_ASSERT(count == 1);
	TEST_ASSERT(mask[0] = 0x80);
}

/** test_bitmask_short() - Test writing a bitmask if the destination storage is too short. */
void test_bitmask_short()
{
	unsigned i, no;
	unsigned size = 16;
	struct sarq sarq;

	unsigned acks[] = { 0, 1, 3, 5, 7, 9, 10, 15 };

	unsigned count;
	unsigned char mask[1];

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	for (i = 0; i < sizeof(acks)/sizeof(*acks); ++i) {
		TEST_ASSERT_(sarq_ack(&sarq, acks[i]) == 0,
				"ack %d", i);
	}

	count = sarq_ack_mask(&sarq, &no, sizeof(mask), mask);
	TEST_ASSERT(no == 2);
	TEST_ASSERT(count == 8);
	TEST_ASSERT(mask[0] = 0xa4);
}

/** test_timeout() - Test rescheduling of packets when a timeout is detected. */
void test_timeout()
{
	unsigned no = 0;
	unsigned size = 8;
	unsigned attempt;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	TEST_ASSERT(sarq_add(&sarq, 1) == 0);

	for (attempt = 0; attempt < sarq.max_tx_attempts; ++attempt) {
		TEST_ASSERT(sarq_next(&sarq, &no) == 0);
		TEST_ASSERT(no == 1);
		TEST_ASSERT(sarq_take(&sarq, no, attempt+1) == 0);
		TEST_ASSERT(sarq_next(&sarq, &no) != 0);
		sarq_retransmit(&sarq, attempt+1);
	}

	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_readd() - Try to add a packet that was already acknowledged. */
void test_readd()
{
	unsigned no = 0;
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	// add something and ack it
	TEST_ASSERT(sarq_add(&sarq, 1) == 0);
	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(no == 1);
	TEST_ASSERT(sarq_ack(&sarq, no) == 0);
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);

	// try to add it again
	TEST_ASSERT(sarq_add(&sarq, 1) != 0);
	TEST_ASSERT(sarq_next(&sarq, &no) != 0);
}

/** test_retransmit() - Test if the sarq_retransmit() function works as expected. */
void test_retransmit()
{
	unsigned no = 0;
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	// we now fill the window
	for (no = 0; no < size; ++no) {
		TEST_ASSERT(sarq_can_transmit(&sarq, no) == 0);
		TEST_ASSERT(sarq_add(&sarq, no) == 0);
		TEST_ASSERT(sarq_can_transmit(&sarq, no) == 1);
	}

	// acknowledge a packet and it should be free again
	TEST_ASSERT(sarq_can_transmit(&sarq, no - size + 1) == 1);
	TEST_ASSERT(sarq_ack(&sarq, 0) == 0);
	TEST_ASSERT(sarq_can_transmit(&sarq, no) == 0);
	TEST_ASSERT(sarq_add(&sarq, no++) == 0);

	// now we move the window and check if this frees up a space
	TEST_ASSERT(sarq_can_transmit(&sarq, no - size + 1) == 1);
	TEST_ASSERT(sarq_get(&sarq, no) != NULL);
	TEST_ASSERT(sarq_can_transmit(&sarq, no) == 0);
}

/** test_ack_reverse() - test acknowledging in reverse order */
void test_ack_reverse()
{
	unsigned no, i;
	unsigned size = 8;
	struct sarq sarq;

	sarq_init(&sarq, size, alloca(size * sizeof(*sarq.items)));

	for (no = 0; no < size; ++no) {
		TEST_ASSERT(sarq_add(&sarq, no) == 0);
	}

	// acknowledge in reverse order
	for (i = 0; i < size; ++i) {
		TEST_ASSERT(sarq_ack(&sarq, size - i - 1) == 0);
	}

	TEST_ASSERT(sarq_add(&sarq, i) == 0);
	TEST_ASSERT(sarq_next(&sarq, &no) == 0);
	TEST_ASSERT(no == i);
}

TEST_LIST = {
	{ "offset", test_offset },
	{ "take", test_take },
	{ "ack", test_ack },
	{ "bulkack", test_bulkack },
	{ "bulkack_partial", test_bulkack_partial },
	{ "bulkack_ahead", test_bulkack_ahead },
	{ "bitmask", test_bitmask },
	{ "bitmask_empty", test_bitmask_empty },
	{ "bitmask_short", test_bitmask_short },
	{ "timeout", test_timeout },
	{ "readd", test_readd },
	{ "retransmit", test_retransmit },
	{ "ack_reverse", test_ack_reverse },
	{ NULL }
};
