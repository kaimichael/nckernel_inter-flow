CODARQ
======

Coded ARQ protocol based on the research paper `A coded generalization of selective repeat ARQ`_.

.. _A coded generalization of selective repeat ARQ: https://www.scss.tcd.ie/doug.leith/pubs/infocom2015.pdf

Configuration
-------------

:symbol_size: Maximum Payload size.
:symbols: Number of packets per generation.
:codec: kodo code to use for encoding and decoding.
:field: Field size for the network code.
:redundancy: Number of redundant packets at the end of a generation.
:max_containers: Maximum number of parallel generations.
:timeout: Retransmission timeout.

API
---

.. kernel-doc:: include/nckernel/codarq.h
