
ACLOCAL_AMFLAGS = -I m4 --install

nobase_astmod_LTLIBRARIES = chan_polygator.la
chan_polygator_la_SOURCES = \
		chan_polygator.c \
		address.c \
		imei.c \
		rtp.c \
		sms.c \
		strutil.c \
		at.c \
		m10.c \
		sim300.c \
		sim900.c

chan_polygator_la_CFLAGS = $(AM_CFLAGS) -Wall
chan_polygator_la_LDFLAGS = $(AM_LDFLAGS) -module -lsqlite3 -lvinetic -lrt

noinst_HEADERS = \
		address.h \
		imei.h \
		at.h \
		m10.h \
		rtp.h \
		sim300.h \
		sim900.h \
		sms.h \
		strutil.h \
		x_timer.h

dist_noinst_DATA = \
		defaults

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
	firmware/cram.byt \
	firmware/flash_nor_16bits_hwasic_evp_4902_rel.hex

CLEANFILES = *~

install-data-hook:
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.so
	@mv $(DESTDIR)$(astmoddir)/chan_polygator.so.0.0.0 $(DESTDIR)$(astmoddir)/chan_polygator.so
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.so.0
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.a
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.la
