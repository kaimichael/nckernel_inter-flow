Quick Start
===========

Dependencies
------------

* C and C++ compiler infrastructure (e.g. gcc or clang)
* kodo-c
* kodo-sliding-window

Compiling
---------

It is recommended to compile the project in a separate directory. We will refer to this
directory as ``$BUILD`` later. This can be done with the following steps::

    mkdir build
    cd build
    cmake ..

This will setup the build system. After the initial configuration you can
compile the project with ``make``::

  make

By default the static library is placed in the ``lib`` directory and the example
programs are in the ``bin`` directory.

Starting Your Project
---------------------

Now that you have built nckernel, you can use it in your own projects. The specific steps
depend on your development environment. In general, you have to make sure that the `include`
directory of this repository is used (``-I`` argument for the compiler), and the static or
dynamic library is passed to the linker.

Another easy generic way is to just install nckernel into your system. Then it should
be found by the standard procedures of your build system::

   make install

In this guide, we will just use the command line to perform all necessary steps. We
will create a new directory for this project next to your copy of nckernel::

   cd ../..
   mkdir encode_decode
   cd encode_decode

Implementing the Encoder
------------------------

The folllowing example code implements a simple encoder program. The program reads lines
from the standard input. The lines are encoded and then sent out over UDP. Write the code
to ``ncsend.c`` and save the file. Compile to program with the following command::

  gcc -I../nckernel/include -L$BUILD ncsend.c -lnckernel -oncsend

This compiles the file and links the executable to the dynamic library for ``nckernel``.

.. literalinclude:: ../examples/ncsend.c
   :language: c

Implementing the Decoder
------------------------

The corresponding decoder is implemented in the following example. The program receives
the encoded UDP packets and writes the lines to standard output. Write the code to
``ncrecv.c`` and save the file. The file can be compiled with the following command::

  gcc -I../nckernel/include -L$BUILD ncrecv.c -lnckernel -oncrecv

This compiles the file and links the executable to the dynamic library for ``nckernel``.

.. literalinclude:: ../examples/ncrecv.c
   :language: c

Testing
-------

Now you should have two executables: ``ncsend`` and ``ncrecv``. You can test
the programs by running ``ncsend`` in one terminal and ``ncrecv`` in the other.
When you write a line in the sender, it will be sent to the receiver and
immediatelly displayed. The result is not very impressive, but the code
demonstrates the most basic operations of ``nckernel``.  You can now modify the
code according to your needs, for example use different codes, use different
send methods, encode and decode a file instead of ``stdin`` and ``stdout``,
etc.
