
ACLOCAL_AMFLAGS = -I m4 --install

nobase_astmod_LTLIBRARIES = chan_pg.la
chan_pg_la_SOURCES = \
		chan_pg.c
chan_pg_la_CFLAGS = $(AM_CFLAGS) -Wall
chan_pg_la_LDFLAGS = -module -lsqlite3 -lvinetic

CLEANFILES = *~

install-data-hook:
	rm $(astmoddir)/chan_pg.so
	mv $(astmoddir)/chan_pg.so.0.0.0 $(astmoddir)/chan_pg.so
	rm $(astmoddir)/chan_pg.so.0
	rm $(astmoddir)/chan_pg.a
	rm $(astmoddir)/chan_pg.la
