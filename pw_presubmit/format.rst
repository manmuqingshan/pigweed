.. _module-pw_presubmit-format:

===============
Code formatting
===============
.. pigweed-module-subpage::
   :name: pw_presubmit

.. admonition:: Note
   :class: warning

   :bug:`326309165`: While the ``pw format`` command interface is very stable,
   the ``pw_presubmit.format`` library is a work-in-progress effort to detach
   the implementation of ``pw format`` from the :ref:`module-pw_presubmit`
   module. Not all formatters are migrated, and the library API is unstable.
   After some of the core pieces land, this library will be moved to
   ``pw_code_format``.

.. _module-pw_presubmit-format-api:

-------------
API reference
-------------

Core
====
.. automodule:: pw_presubmit.format.core
   :members:
   :special-members: DiffCallback
   :noindex:

Formatters
==========
.. autoclass:: pw_presubmit.format.bazel.BuildifierFormatter
   :members:
   :noindex:

.. autoclass:: pw_presubmit.format.cpp.ClangFormatFormatter
   :members:
   :noindex:

.. autoclass:: pw_presubmit.format.gn.GnFormatter
   :members:
   :noindex:

.. autoclass:: pw_presubmit.format.python.BlackFormatter
   :members:
   :noindex:

.. autoclass:: pw_presubmit.format.rust.RustfmtFormatter
   :members:
   :noindex:
