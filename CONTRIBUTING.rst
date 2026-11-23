Contributing to 400 SSD Term
============================

Thanks for your interest in improving 400 SSD Term.

Principles
----------

* Keep the code small and readable. A feature that needs a large amount of
  code, or that can live in the configuration header, should be discussed
  first.
* Configuration is compile time only and lives in ``config.def.h``.
* Linux and X11 are the only supported targets.

Code style
----------

* The language is C11.
* Indent with tabs, width 4. The ``.editorconfig`` file describes the rest.
* Do not write comments in the code. Make names and structure explain intent.
* Do not prefix names with the project name. Write ``name()`` in
  ``name.c``, not ``proj_name()`` in ``proj_name.c``.
* Functions that belong to one module share a short module prefix such as
  ``term_`` or ``win_``, and everything private to a file is ``static``.
* The code must build without warnings with the flags from the ``Makefile``.

Building and testing
--------------------

::

    make clean
    make

Run the terminal and check at least: typing, colors, scrollback, selection,
resizing and a full screen program such as ``less`` or ``vim``. Headless
checks are possible with ``xvfb-run``.

Submitting changes
------------------

1. Fork the repository and create a topic branch.
2. Keep each commit focused and write an imperative commit message.
3. Make sure ``make clean && make`` prints no warnings.
4. Open a pull request that explains what changed and why.

By contributing you agree that your work is licensed under GPL-3.0-or-later.
