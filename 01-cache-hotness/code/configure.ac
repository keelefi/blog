AC_INIT([cache-hotness], [1.2], [https://github.com/keelefi/blog])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AX_PROG_HELP2MAN

AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC

AC_PATH_PROG([SETCAP], [setcap], [/usr/sbin/setcap], [$PATH:/usr/sbin:/sbin])

AC_CONFIG_FILES([
    Makefile
])

AC_OUTPUT
