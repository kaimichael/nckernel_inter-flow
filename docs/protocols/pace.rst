PACE
====

Protocol based on noack but with paced redundancy and rate feedbacks. Suited for high RTT applications.

Configuration
-------------

:symbol_size: Maximum Payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.
:redundancy: Number of redundant packets per generation.
:pace_redundancy: Percentage of redundant packets per generation.
:tail_redundancy: Number of packets at the end of a generation.
:timeout: Retransmission timeout.

API
---

.. kernel-doc:: include/nckernel/pace.h
