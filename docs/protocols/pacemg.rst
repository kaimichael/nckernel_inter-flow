PaceMG
======

Protocol based on noack but with paced redundancy, rate feedbacks and multiple generations.
Suited for high RTT applications.

Configuration
-------------

:symbol_size: Maximum payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.
:redundancy: Number of redundant packets per generation.
:coding_ration: Percentage of redundant packets per generation.
:tail_packets: Number of packets at the end of a generation.
:timeout: Retransmission timeout.

API
---

.. kernel-doc:: include/nckernel/pacemg.h
