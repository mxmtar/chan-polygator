
ACLOCAL_AMFLAGS = -I m4 --install

nobase_astmod_LTLIBRARIES = chan_polygator.la
chan_polygator_la_SOURCES = \
		chan_polygator.c \
		address.c \
		imei.c \
		sms.c \
		strutil.c \
		at.c \
		m10.c \
		sim300.c \
		sim900.c

chan_polygator_la_CFLAGS = $(AM_CFLAGS) -Wall
chan_polygator_la_LDFLAGS = $(AM_LDFLAGS) -module -lsqlite3 -lvinetic -lrt

bin_PROGRAMS = sim900bfw sim900imei sim900dump
sim900bfw_SOURCES = \
		sim900bfw.c

sim900bfw_CFLAGS = $(AM_CFLAGS) -Wall
sim900bfw_LDFLAGS = $(AM_LDFLAGS) -lrt

sim900imei_SOURCES = \
		sim900imei.c \
		imei.c \
		strutil.c

sim900imei_CFLAGS = $(AM_CFLAGS) -Wall
sim900imei_LDFLAGS = $(AM_LDFLAGS) -lrt

sim900dump_SOURCES = \
		sim900dump.c

sim900dump_CFLAGS = $(AM_CFLAGS) -Wall
sim900dump_LDFLAGS = $(AM_LDFLAGS) -lrt

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
	firmware/1137B08SIM900M64_ST_DTMF_JD_MMS.cla \
	firmware/1137B09SIM900M64_ST_DTMF_JD_MMS.cla \
	firmware/flash_nor_16bits_hwasic_evp_4902_rel.hex

CLEANFILES = *~

install-data-hook:
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.so
	@mv $(DESTDIR)$(astmoddir)/chan_polygator.so.0.0.0 $(DESTDIR)$(astmoddir)/chan_polygator.so
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.so.0
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.a
	@rm $(DESTDIR)$(astmoddir)/chan_polygator.la
