
ACLOCAL_AMFLAGS = -I m4 --install

nobase_astmod_LTLIBRARIES = chan_pg.la
chan_pg_la_SOURCES = \
		chan_pg.c \
		at.c
chan_pg_la_CFLAGS = $(AM_CFLAGS) -Wall
chan_pg_la_LDFLAGS = $(AM_LDFLAGS) -module -lsqlite3 -lvinetic

firmwaredir = $(astdatadir)/polygator
dist_firmware_DATA = \
	firmware/edspPRAMfw_RTP_782_14.bin \
	firmware/edspDRAMfw_RTP_782_14.bin \
	firmware/edspPRAMfw_RTP_0_15_56_V14.bin \
	firmware/edspDRAMfw_RTP_0_15_56_V14.bin \
	firmware/edspPRAMfw_RTP_16_15_248_V14.bin \
	firmware/edspDRAMfw_RTP_16_15_248_V14.bin \
	firmware/edspPRAMfw_RTP_24_15_248_V14.bin \
	firmware/edspDRAMfw_RTP_24_15_248_V14.bin \
	firmware/edspPRAMfw_RTP_20_15_264_V14.bin \
	firmware/edspDRAMfw_RTP_20_15_264_V14.bin \
	firmware/ALM_2484_AB_01.dwl \
	firmware/ALM_2484_CD_01.dwl \
	firmware/cram.byt

CLEANFILES = *~

install-data-hook:
	rm $(astmoddir)/chan_pg.so
	mv $(astmoddir)/chan_pg.so.0.0.0 $(astmoddir)/chan_pg.so
	rm $(astmoddir)/chan_pg.so.0
	rm $(astmoddir)/chan_pg.a
	rm $(astmoddir)/chan_pg.la
