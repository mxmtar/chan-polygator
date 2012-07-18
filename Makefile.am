
ACLOCAL_AMFLAGS = -I m4 --install

nobase_astmod_LTLIBRARIES = chan_polygator.la
chan_polygator_la_SOURCES = \
		chan_polygator.c \
		address.c \
		sms.c \
		strutil.c \
		at.c \
		m10.c \
		sim300.c \
		sim900.c

chan_polygator_la_CFLAGS = $(AM_CFLAGS) -Wall
chan_polygator_la_LDFLAGS = $(AM_LDFLAGS) -module -lsqlite3 -lvinetic

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
	rm $(astmoddir)/chan_polygator.so
	mv $(astmoddir)/chan_polygator.so.0.0.0 $(astmoddir)/chan_polygator.so
	rm $(astmoddir)/chan_polygator.so.0
	rm $(astmoddir)/chan_polygator.a
	rm $(astmoddir)/chan_polygator.la
