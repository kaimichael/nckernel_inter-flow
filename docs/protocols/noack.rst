NoAck
=====

Uses a generation-based network code to add a fixed number of coded packets at the
end of the generation. It does not use any form of acknowledgements.

Configuration
-------------

:symbol_size: Maximum Payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.
:redundancy: Number of redundant packets at the end of a generation.
:timeout: Time to wait for a new packet before the generation is closed.
:systematic: Disable/Enable systematic mode in encoder

API
---

.. kernel-doc:: include/nckernel/noack.h
