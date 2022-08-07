bin_PROGRAMS = cache-hotness
cache_hotness_SOURCES = \
    cache-hotness.c

if HAVE_HELP2MAN
  man1_MANS = $(ax_help2man_MANS)
  @AX_HELP2MAN_RULES@
endif

install-exec-hook:
	$(SETCAP) cap_sys_nice,cap_dac_read_search=ep $(DESTDIR)$(bindir)/cache-hotness
