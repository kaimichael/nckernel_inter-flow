NoCode
======

Send packets without any modification. This protocol copies source packets to coded
packets. Because it does no processing, it can be used to verify that your application
works correctly with nckernel protocols.

Configuration
-------------

:symbol_size: Maximum payload size.

API
---

.. kernel-doc:: include/nckernel/nocode.h
