Interface
=========

Encoder
-------

.. c:type:: struct nck_encoder
    Structure representing an encoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_create_encoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_put_source nck_full

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_has_coded nck_get_coded

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_put_feedback

Decoder
-------

.. c:type:: struct nck_decoder
    Structure representing a decoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_create_decoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_has_source nck_get_source

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_put_coded

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_has_feedback nck_get_feedback

Recoder
-------

.. c:type:: struct nck_recoder
    Structure representing a recoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_create_recoder

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_put_coded nck_has_coded nck_get_coded

.. kernel-doc:: include/nckernel/nckernel.h
    :functions: nck_has_feedback nck_get_feedback nck_put_feedback

Generic Interface
-----------------

.. c:type:: struct nck_coder
    Generic structure representing a coder instance.

This structure allows to write generic code using any type of coder. For
example both an encoder and a recoder can produce coded packets and a
generic function can be written that sends coded packets from both an
encoder and a decoder by using the :c:type:`nck_coder` structure instead.

Pointers to the :c:type:`nck_encoder`, :c:type:`nck_decoder` and
:c:type:`nck_recoder` structures can safely be casted to a pointer to a
`nck_coder`. However calling a function that is not defined by the underlying
coder, e.g. :c:func:`nck_get_coded` on a :c:type:`nck_decoder` will trigger
undefined behavior (usually dereferencing `NULL`).
