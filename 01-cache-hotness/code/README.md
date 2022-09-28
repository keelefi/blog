# cache-hotness

`cache-hotness` is a benchmark program to see the effect of "cache hotness" in task scheduling. `cache-hotness` builds
with Autoconf and ships a man-page.

## Installing

Fetch source tarball:

    $ wget https://github.com/keelefi/blog/releases/download/cache-hotness-v1.4/cache-hotness-1.4.tar.gz

Untar:

    $ tar xvf cache-hotness-1.4.tar.gz

Change directory:

    $ cd cache-hotness-1.4/

Configure:

    $ ./configure

Build:

    $ make

Install:

    $ sudo make install

Note: `make install` requires root privileges even if installing to a user directory with `DESTDIR`. This is because the
install step will use `setcap` to set extra capabilities. The extra capabilities are `CAP_SYS_NICE` and
`CAP_DAC_READ_SEARCH`.
