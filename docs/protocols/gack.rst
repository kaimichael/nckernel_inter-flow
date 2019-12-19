GAck
====

Implements a rateless network code. It sends coded packets for a generation until
an acknowledgement is received. It does not do any rate limiting and can easily
flood your network, so use it with care.

Configuration
-------------

:symbol_size: Maximum Payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.

API
---

.. kernel-doc:: include/nckernel/gack.h
