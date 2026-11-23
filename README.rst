400ssd
======

400ssd is a small terminal emulator for Linux written in C on top of
Xlib and Xft. It follows the suckless approach: a short code base and
configuration done at compile time by editing a header file.

Features
--------

* UTF-8, wide characters, 256 colors and truecolor
* Bold, italic, underline, reverse and strikethrough attributes
* Alternate screen, scroll regions and the line drawing charset
* Scrollback with mouse wheel and Shift+PageUp / Shift+PageDown
* Autoscroll switch in the configuration
* PRIMARY and CLIPBOARD selections, word and line selection by multiple clicks
* Bracketed paste
* Font fallback through fontconfig
* Resizable window, window title changes through OSC sequences

Requirements
------------

* A C compiler (GCC or Clang) and GNU make
* pkg-config
* Xlib, Xft and fontconfig development files
* A running X server (or XWayland)

Build and install
-----------------

Build the project::

    make

Install it system-wide::

    sudo make install

The first build creates ``config.h`` from ``config.def.h``.

``make install`` installs:

* The ``400ssd`` binary into ``/usr/bin``
* The manual page ``400ssd.1`` into ``/usr/share/man/man1``
* ``400ssd.desktop`` into ``/usr/share/applications``

When installing directly into the system, the install target also attempts
to refresh the manual page database using ``mandb`` or ``makewhatis``, and
update the desktop application database when the corresponding utility is
available.

To install into a staging directory, use ``DESTDIR``::

    make install DESTDIR=/tmp/400ssd-package

To remove everything installed by the install target::

    sudo make uninstall

Usage
-----

::

    400ssd [-v] [-f font] [-t title] [-c class] [-e command [args ...]]

Configuration
-------------

Edit ``config.h`` and run ``make`` again.

Available options:

* ``font``: fontconfig font description, for example
  ``monospace:size=11``
* ``colors``: the 16 ANSI colors
* ``foreground``, ``background``, ``cursorcolor``: default colors
* ``selectionfg``, ``selectionbg``: colors of selected text
* ``defcols``, ``defrows``: initial window size in cells
* ``border``: padding around the text in pixels
* ``autoscroll``: when set to ``1``, the view jumps to the bottom whenever
  new output arrives. When set to ``0``, the view stays where it is after
  you scroll up and you have to scroll down yourself with the mouse wheel
* ``scrollback``: number of history lines, ``0`` disables the history
* ``wheelstep``: lines scrolled per wheel notch
* ``bell``: ring the X bell on BEL
* ``termname``: value of ``TERM`` for child processes
* ``shell``: fallback shell when neither ``SHELL`` nor the passwd entry is set
* ``worddelimiters``: characters that end a word on double click

Key bindings
------------

* ``Ctrl+Shift+C``: copy selection to the clipboard
* ``Ctrl+Shift+V``: paste from the clipboard
* ``Shift+Insert``: paste the primary selection
* ``Shift+PageUp`` / ``Shift+PageDown``: scroll one page
* Left button: select, double click selects a word, triple click a line
* Middle button: paste the primary selection

Installing dependencies and building
------------------------------------

Debian and Ubuntu
~~~~~~~~~~~~~~~~~

::

    sudo apt update
    sudo apt install build-essential pkg-config libx11-dev libxft-dev libfontconfig1-dev
    make
    sudo make install

Arch Linux
~~~~~~~~~~

::

    sudo pacman -S --needed base-devel pkgconf libx11 libxft fontconfig
    make
    sudo make install

Void Linux
~~~~~~~~~~

::

    sudo xbps-install -S base-devel pkg-config libX11-devel libXft-devel fontconfig-devel
    make
    sudo make install

License
-------

GPL-3.0-or-later. See the ``LICENSE`` file.
