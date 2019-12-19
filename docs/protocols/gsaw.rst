GSAW
====

GSAW is the Generational Stop And Wait protocol. It uses network coding to add
redundant packets. It will send a fixed number of redundant packets and then waits
for an acknowledgement. When no acknowledgement is received within a specified timeout,
a coded packet will be retransmitted. Because it waits for an acknowledgement and
does not send any data in this time, the throughput will be pretty bad in networks
with large bandwidth delay product.

Configuration
-------------

:symbol_size: Maximum Payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.
:redundancy: Number of redundant packets at the end of a generation.
:timeout: Retransmission timeout.

API
---

.. kernel-doc:: include/nckernel/gsaw.h
