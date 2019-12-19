Sliding Window
==============

Implements sliding window RLNC with optional feedback.

Configuration
-------------

:symbol_size: Maximum payload size.
:symbols: Number of packets in the sliding window.
:timeout: Retransmission timeout.
:redundancy: Number of redundant packets per window.
:systematic: Number of consecutive systematic packets to send before the next coded packet.
:coded: Number of consecutive coded packets to send before the next systematic packet.
:forward_code_window: Number of packets in the encoding window.
:feedback: 1 - enable feedback (default); 0 - disable feedback.
:coded_retransmissions: 0 - send systematic retransmissions (default); 1 - send only coded retransmissions.
:memory: Number of coded packets to remember when considering retransmissions.
:tx_attempts: Number of allowed retransmissions per source packet.
:sequence: Initial sequence number.
:feedback_only_on_repair: Send feedback only when a retransmission is required.

API
---

.. kernel-doc:: include/nckernel/sw.h
