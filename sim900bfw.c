/******************************************************************************/
/* sim900bfw.c                                                                */
/******************************************************************************/

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "polygator/polygator-base.h"

#include "autoconfig.h"

#include "x_timer.h"

unsigned char sim900_set_storage_equipment[9] = {
	0x04,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x00, 0x00,
};

unsigned char sim900_configuration_for_erased_area[9] = {
	0x09,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x7f, 0x00,
};

unsigned char sim900_set_for_downloaded_code_information[9] = {
	0x04,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x00, 0x00,
};

unsigned char sim900_set_for_downloaded_code_section[5] = {
	0x01,
	0x00, 0x08, 0x00, 0x00,
};

unsigned char sim900_comparision_for_downloaded_information[13] = {
	0x15,
	0x00, 0x00, 0x00, 0x90,
	0xE7, 0xDA, 0x45, 0x0D,
	0x00, 0x00, 0x00, 0x00,
};

const char *sim900bfw_usage = "Usage: sim900bfw -d <device> [-f <firmware>] [-h <File of Intel HEX>]\n";

const char *intel_hex_default = "flash_nor_16bits_hwasic_evp_4902_rel.hex";
const char *sim900_firmware_default = "1137B09SIM900M64_ST_DTMF_JD_MMS.cla";

static int pg_channel_gsm_power_set(const char *board, int position, int state)
{
	FILE *fp;
	int res = -1;

	if (board) {
		if ((fp = fopen(board, "w"))) {
			fprintf(fp, "GSM%u PWR=%d", position, state);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
#if 0
static int pg_channel_gsm_vio_get(const char *board, int position)
{
	FILE *fp;
	char buf[256];
	char name[64];
	char sim[64];
	char type[64];
	unsigned int pos;
	unsigned int vin_num;
	char vc_type[4];
	unsigned int vc_slot;
	unsigned int vio;
	int res = -1;

	if (board) {
		if ((fp = fopen(board, "r"))) {
			while (fgets(buf, sizeof(buf), fp))
			{
				if (sscanf(buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z/!-] %[0-9A-Za-z/!-] VIN%u%[ACMLP]%u VIO=%u", &pos, type, name, sim, &vin_num, vc_type, &vc_slot, &vio) == 8) {
					if (pos == position) {
						res = vio;
						break;
					}
				}
			}
			fclose(fp);
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
#endif
static int pg_channel_gsm_serial_set(const char *board, int position, int serial)
{
	FILE *fp;
	int res = -1;

	if (board) {
		if ((fp = fopen(board, "w"))) {
			fprintf(fp, "GSM%u SERIAL=%d", position, serial);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}

static int pg_channel_gsm_key_press(const char *board, int position, int state)
{
	FILE *fp;
	int res = -1;

	if (board) {
		if ((fp = fopen(board, "w"))) {
			fprintf(fp, "GSM%u KEY=%d", position, state);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}

int main(int argc, char *argv[])
{
#if 0
	struct timeval timeout;
	char cbuf[256];
	FILE *ih_fp;
	int rc;
	int data_int;
	int module_type;

	size_t cnt;

	union {
		char byte[4];
		u_int32_t full;
	} res_checksum;
#else
	int opt;

	char *device = NULL;
	char *hex = NULL;
	char *firmware = NULL;
	char *imei = NULL;

	size_t i;
	char t_buf[512];

	char *channel;
	u_int32_t pos_on_board;

	char type[64];
	char tty[64];
	char sim[64];
	unsigned int pos;
	unsigned int vin_num;
	char vc_type[4];
	unsigned int vc_slot;
	unsigned int vio;

	char board_fpath[PATH_MAX];
	FILE *board_fptr;
	struct stat board_fstat;

	char tty_fpath[PATH_MAX];
	int tty_fd = -1;
	struct termios tty_termios;

	char hex_fpath[PATH_MAX];

	char fw_fpath[PATH_MAX];
	int fw_fd = -1;
	struct stat fw_stat;
	size_t fw_size;
	size_t fw_block_count;
	unsigned char fw_block[0x800];
	size_t fw_block_size;
	u_int32_t fw_checksum;
	u_int8_t fw_u8;

	struct x_timer timer;
	char chr;
	int res;
	int cur;

#endif

	while ((opt = getopt(argc, argv, "d:f:h:i:")) != -1)
	{
		switch (opt)
		{
			case 'd': /*! - device, for example "board-k32pci-pci-234-gsm2" */
				device = optarg;
				break;
			case 'f': /*! - frimware, default "1137B09SIM900M64_ST_DTMF_JD_MMS.cla" */
				firmware = optarg;
				break;
			case 'h': /*! - File of Intel HEX, default "flash_nor_16bits_hwasic_evp_4902_rel.hex" */
				hex = optarg;
				break;
			case 'i':
				imei = optarg;
				break;
			default: /*! '?' */
				printf(sim900bfw_usage);
				goto main_end;
		}
	}

	if (!device) {
		printf("device not specified\n");
		printf(sim900bfw_usage);
		goto main_end;
	}

	if (hex)
		snprintf(hex_fpath, sizeof(hex_fpath), "%s", hex);
	else
		snprintf(hex_fpath, sizeof(hex_fpath), "%s/polygator/%s", ASTERISK_DATA_DEFAULT_PATH, intel_hex_default);

	if (firmware)
		snprintf(fw_fpath, sizeof(fw_fpath), "%s", firmware);
	else
		snprintf(fw_fpath, sizeof(fw_fpath), "%s/polygator/%s", ASTERISK_DATA_DEFAULT_PATH, sim900_firmware_default);

	if (imei) imei = NULL;

	printf("Discovering specified device \"%s\"...\n", device);
	if (!(channel = strrchr(device, '-'))) {
		printf("channel part not found\n");
		goto main_end;
	}
	*channel++ = '\0'; // terminating board part
	printf("Board: %s...", device);
	snprintf(board_fpath, sizeof(board_fpath), "/dev/polygator/%s", device);
	if (stat(board_fpath, &board_fstat) < 0) {
		printf("not found: %s\n", strerror(errno));
		goto main_end;
	}
	printf("present\n");
	printf("Channel: %s...", channel);
	if ((sscanf(channel, "GSM%u", &pos_on_board) != 1) &&
		(sscanf(channel, "gsm%u", &pos_on_board) != 1)) {
		printf("has wrong name\n");
		goto main_end;
	}
	// open board file
	if (!(board_fptr = fopen(board_fpath, "r"))) {
		printf("unable to scan Polygator board \"%s\": %s\n", board_fpath, strerror(errno));
		goto main_end;
	}
	while (fgets(t_buf, sizeof(t_buf), board_fptr))
	{
		if (sscanf(t_buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z/!-] %[0-9A-Za-z/!-] VIN%u%[ACMLP]%u VIO=%u", &pos, type, tty, sim, &vin_num, vc_type, &vc_slot, &vio) == 8) {
			if ((!strcasecmp(type, "SIM900")) && (pos_on_board == pos)) {
				for (i=0; i<strlen(tty); i++) if (tty[i] == '!') tty[i] = '/';
				snprintf(tty_fpath, sizeof(tty_fpath), "/dev/%s", tty);
				printf("tty device \"%s\"\n", tty_fpath);
				break;
			}
		}
		tty[0] = '\0';
	}
	fclose(board_fptr);

	if (!strlen(tty)) {
		printf("not found\n");
	}

	printf("Starting Download SIM900 Firmware\n");

	// open TTY device
	if ((tty_fd = open(tty_fpath, O_RDWR | O_NONBLOCK)) < 0) {
		printf("can't open \"%s\": %s\n", tty_fpath, strerror(errno));
		goto main_end;
	}
	// set termios
	cfmakeraw(&tty_termios);
	if (tcgetattr(tty_fd, &tty_termios)) {
		printf("tcgetattr() error: %s\n", strerror(errno));
		goto main_end;
	}
	cfsetispeed(&tty_termios, B115200);
	cfsetospeed(&tty_termios, B115200);
	if (tcsetattr(tty_fd, TCSANOW, &tty_termios) < 0) {
		printf("tcsetattr() error: %s\n", strerror(errno));
		goto main_end;
	}
	if (pg_channel_gsm_serial_set(board_fpath, pos_on_board, 0) < 0) {
		printf("pg_channel_gsm_serial_set() error: %s\n", strerror(errno));
		goto main_end;
	}
	if (tcflush(tty_fd, TCIOFLUSH) < 0)
		printf("can't flush tty device: %s\n", strerror(errno));

	// Preparing SIM900 Firmware
	printf("Preparing SIM900 Firmware...");
	// open firmware file
	if ((fw_fd = open(fw_fpath, O_RDONLY)) < 0) {
		printf("can't open \"%s\": %s\n", fw_fpath, strerror(errno));
		goto main_end;
	}
	// get firmware size
	if (fstat(fw_fd, &fw_stat) < 0) {
		printf("can't stat \"%s\": %s\n", fw_fpath, strerror(errno));
		goto main_end;
	}
	fw_size = fw_stat.st_size;
	printf("size=%ld ", fw_stat.st_size);
	fw_block_count = (fw_stat.st_size/2048) + ((fw_stat.st_size%2048)?(1):(0));
	printf("block_count=%lu ", (long unsigned int)fw_block_count);
	// calc firmware checksum
	fw_checksum = 0;
	for (i=0; i<fw_stat.st_size; i++)
	{
		if (lseek(fw_fd, i, SEEK_SET) < 0) {
			printf("can't lseek \"%s\": %s\n", fw_fpath, strerror(errno));
			goto main_end;
		}
		if (read(fw_fd, &fw_u8, sizeof(u_int8_t)) < 0) {
			printf("can't read \"%s\": %s\n", fw_fpath, strerror(errno));
			goto main_end;
		}
		fw_checksum += fw_u8;
	}
	printf("checksum=%08x ", fw_checksum);
	printf("succeeded\n");
	fflush(stdout);

	// Detection of synchronous bytes
	printf("Detection of synchronous bytes...");
	fflush(stdout);

	if (pg_channel_gsm_power_set(board_fpath, pos_on_board, 1) < 0) {
		printf("pg_channel_gsm_power_set() error: %s\n", strerror(errno));
		goto main_end;
	}
	sleep(3);
	if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 1) < 0) {
		printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
		goto main_end;
	}
	usleep(1999999);
	if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 0) < 0) {
		printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
		goto main_end;
	}

	x_timer_set_second(timer, 60);
	do {
		// writing synchronous octet
		chr = 0x16;
		if (write(tty_fd, &chr, 1) < 0) {
			printf("failed - write(): %s\n", strerror(errno));
			goto main_end;
		}
		// wait for synchronous octet round trip
		usleep(30000);
		// wait for synchronous octet
		res = read(tty_fd, &chr, 1);
		if (res < 0) {
			if (errno != EAGAIN) {
				printf("failed - read(): %s\n", strerror(errno));
				goto main_end;
			}
		} else if (res == 1) {
			if (chr == 0x16) break;	// synchronous octet received
		}
	} while (is_x_timer_active(timer));
	// check for entering into downloading procedure
	if (is_x_timer_fired(timer)) {
		printf("run in normal mode - quit from program\n");
		goto main_end;
	}
	// entering into downloading procedure
	printf("module entered into downloading procedure\n");
#if 0
	// File of Intel HEX download
	printf("File of Intel HEX download...");
	fflush(stdout);
	if (!(ih_fp = fopen(intel_hex, "r"))) {
		printf("failed - fopen(%s): %s\n", intel_hex, strerror(errno));
		goto main_end;
	}
	rc = 0;
	// copy hex file into device
	while (fgets(cbuf, sizeof(cbuf), ih_fp))
	{
		data_int = strlen(cbuf);
		if (write(sig_fd, cbuf, data_int) < 0) {
			printf("failed - write(): %s\n", strerror(errno));
			fclose(ih_fp);
			goto main_end;
		}
		printf(".");
		fflush(stdout);
		rc += data_int;
		usleep(data_int*100);
	}
	data_int = rc;
	// wait for success download indication
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - read(): %s\n", strerror(errno));
				fclose(ih_fp);
				goto main_end;
			}
		} else if(rc == 1) {
			if (chr == 0) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - timeout\n");
		fclose(ih_fp);
		goto main_end;
	}
	fclose(ih_fp);
	printf("\nsucceeded - total %d bytes\n", data_int);

	// Set the storage equipment
	printf("Set the storage equipment...");
	data_int = sizeof(sim900_set_storage_equipment);
	if (write(sig_fd, sim900_set_storage_equipment, data_int) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(data_int*100);
	// wait for success indication
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - read(): %s\n", strerror(errno));
				goto main_end;
			}
		} else if (rc == 1) {
			if (chr == 0x04) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)){
		printf("failed -  timeout\n");
		goto main_end;
	}
	printf("succeeded\n");

	// Read the flash manufacturer information
	printf("Read the flash manufacturer information...");
	fflush(stdout);
	chr = 0x02;
	if (write(sig_fd, &chr, 1) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(100);
	// read marker
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - marker - read(): %s\n", strerror(errno));
				goto main_end;
				}
		} else if (rc == 1) {
			if (chr == 0x02) break;
		}
		usleep(100);
	} while (is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - marker - timeout\n");
		goto main_end;
	}
	// read data
	for (cnt=0; cnt<4; cnt++)
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		x_timer_set(timer, timeout);
		do {
			rc = read(sig_fd, &chr, 1);
			if (rc < 0) {
				if (errno != EAGAIN) {
					printf("failed - read(): %s\n", strerror(errno));
					goto main_end;
				}
			} else if (rc == 1) {
				break;
			}
			usleep(100);
		} while (is_x_timer_active(timer));
		// check for completion
		if (is_x_timer_fired(timer)) {
			printf("failed - timeout\n");
			goto main_end;
		}
	}
	printf("succeeded\n");

	// Configuration for erased area of FLASH
	printf("Configuration for erased area of FLASH...");
	fflush(stdout);
	data_int = sizeof(sim900_configuration_for_erased_area);
	if (write(sig_fd, sim900_configuration_for_erased_area, data_int) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(data_int*100);
	// wait for success indication
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - read(): %s\n", strerror(errno));
				goto main_end;
			}
		} else if (rc == 1) {
			if (chr == 0x09) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - timeout\n");
		goto main_end;
	}
	printf("succeeded\n");

	// FLASH erase
	printf("FLASH erase...");
	fflush(stdout);
	chr = 0x03;
	if (write(sig_fd, &chr, 1) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(100);
	// read marker
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - marker - read(): %s\n", strerror(errno));
				goto main_end;
			}
		} else if (rc == 1) {
			if (chr == 0x03) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - marker - timeout\n");
		goto main_end;
	}
	// Wait for flash erase
	timeout.tv_sec = 300;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - read(): %s\n", strerror(errno));
				goto main_end;
			}
		} else if (rc == 1) {
			if (chr == 0x30) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - timeout\n");
		goto main_end;
	}
	printf("succeeded\n");

	// Set for downloaded code information
	printf("Set for downloaded code information...");
	fflush(stdout);
	memcpy(&sim900_set_for_downloaded_code_information[5], &fw_stat.st_size, 4);
	data_int = sizeof(sim900_set_for_downloaded_code_information);
	if (write(sig_fd, sim900_set_for_downloaded_code_information, data_int) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(data_int*100);
	// read marker
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - marker - read(): %s\n", strerror(errno));
				goto main_end;
			}	
		} else if (rc == 1) {
			if (chr == 0x04) break;
		}
		usleep(100);
	} while(is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - marker - timeout\n");
		goto main_end;
	}
	printf("succeeded\n");

	for (cnt=0; cnt<fw_block_count; cnt++)
	{
		fw_block_size = (fw_size/0x800)?(0x800):(fw_size);
		printf("\rCode page download - block(%lu/%lu)...", (long unsigned int)cnt+1, (long unsigned int)fw_block_count);
		fflush(stdout);
		fw_size -= fw_block_size;
		
		if (lseek(fw_fd, cnt*0x800, SEEK_SET) < 0) {
			printf("failed - lseek(): %s\n", strerror(errno));
			goto main_end;
		}
		if (read(fw_fd, fw_block, fw_block_size) < 0) {
			printf("failed - read(): %s\n", strerror(errno));
			goto main_end;
		}
		// Set for downloaded code section
		memcpy(&sim900_set_for_downloaded_code_section[1], &fw_block_size, 2);
		rc = sizeof(sim900_set_for_downloaded_code_section);
		if (write(sig_fd, sim900_set_for_downloaded_code_section, rc) < 0) {
			printf("failed section(%lu) write(): %s\n", (long unsigned int)cnt+1, strerror(errno));
			goto main_end;
		}
		usleep(rc*100);
		// read marker
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		x_timer_set(timer, timeout);
		do {
			rc = read(sig_fd, &chr, 1);
			if (rc < 0) {
				if (errno != EAGAIN) {
					printf("failed section(%lu) - marker - read(): %s\n", (long unsigned int)cnt+1, strerror(errno));
					goto main_end;
				}
			} else if (rc == 1) {
				if (chr == 0x01) break;
			}
			usleep(100);
		} while(is_x_timer_active(timer));
		// check for completion
		if (is_x_timer_fired(timer)) {
			printf("failed section(%lu) - marker - timeout\n", (long unsigned int)cnt+1);
			goto main_end;
		}
		// Download code data
		if (write(sig_fd, fw_block, fw_block_size) < 0) {
			printf("failed - write(): %s\n", strerror(errno));
			goto main_end;
		}
		usleep(fw_block_size*100);
		// read marker
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		x_timer_set(timer, timeout);
		do {
			rc = read(sig_fd, &chr, 1);
			if (rc < 0) {
				if (errno != EAGAIN) {
					printf("failed - marker(0x2e) - read(): %s\n", strerror(errno));
					goto main_end;
				}
			} else if (rc == 1) {
				if (chr == 0x2e) break;
			}
			usleep(100);
		} while(is_x_timer_active(timer));
		// check for completion
		if (is_x_timer_fired(timer)) {
			printf("failed - marker(0x2e) - timeout\n");
			goto main_end;
		}
		// read marker
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		x_timer_set(timer, timeout);
		do {
			rc = read(sig_fd, &chr, 1);
			if (rc < 0) {
				if (errno != EAGAIN) {
					printf("failed - marker(0x30) - read(): %s\n", strerror(errno));
					goto main_end;
				}
			} else if (rc == 1) {
				if (chr == 0x30) break;
			}
			usleep(100);
		} while(is_x_timer_active(timer));
		// check for completion
		if (is_x_timer_fired(timer)) {
			printf("failed - marker(0x30) - timeout\n");
			goto main_end;
		}
	}
	printf("succeeded\n");

	// Comparision for downloaded information
	printf("Comparision for downloaded information...");
	fflush(stdout);
	memcpy(&sim900_comparision_for_downloaded_information[5], &fw_checksum, 4);
	memcpy(&sim900_comparision_for_downloaded_information[9], &fw_stat.st_size, 4);
	rc = sizeof(sim900_comparision_for_downloaded_information);
	if (write(sig_fd, sim900_comparision_for_downloaded_information, rc) < 0) {
		printf("failed - write(): %s\n", strerror(errno));
		goto main_end;
	}
	usleep(rc*100);
	// read marker
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - marker(0x15) - read(): %s\n", strerror(errno));
				goto main_end;
				}
		} else if (rc == 1) {
			if (chr == 0x15) break;
		}
		usleep(100);
	} while (is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - marker(0x15) - timeout\n");
		goto main_end;
	}
	// read data
	for (cnt=0; cnt<4; cnt++)
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		x_timer_set(timer, timeout);
		do {
			rc = read(sig_fd, &chr, 1);
			if (rc < 0) {
				if (errno != EAGAIN) {
					printf("failed - read(): %s\n", strerror(errno));
					goto main_end;
				}
			} else if (rc == 1) {
				res_checksum.byte[cnt] = chr;
				break;
			}
			usleep(100);
		} while (is_x_timer_active(timer));
		// check for completion
		if (is_x_timer_fired(timer)) {
			printf("failed - timeout\n");
			goto main_end;
		}
	}
	// read marker
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	x_timer_set(timer, timeout);
	do {
		rc = read(sig_fd, &chr, 1);
		if (rc < 0) {
			if (errno != EAGAIN) {
				printf("failed - marker(0x30) - read(): %s\n", strerror(errno));
				goto main_end;
				}
		} else if (rc == 1) {
			if ((chr == 0x30) || (chr == 0x43)) break;
		}
		usleep(100);
	} while (is_x_timer_active(timer));
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - marker(0x30) - timeout\n");
		goto main_end;
	}
	if (chr == 0x30) {
		printf("checksum match=%08x\n", res_checksum.full);
		printf("Download SIM900 Firmware succeeded\n");
	} else {
		printf("checksum not match=%08x\n", res_checksum.full);
		printf("Download SIM900 Firmware failed\n");
	}
#endif
main_end:
#if 0
	// send power control command - enable GSM module
	pwr_cmd.enable = 0;
	pwr_cmd.activation = 0;
	ioctl(sig_fd, EGGSM_PWR_CFG_SET, &pwr_cmd);
#endif
	close(tty_fd);
	close(fw_fd);
	exit(EXIT_SUCCESS);
}

/******************************************************************************/
/* end of sim900bfw.c                                                         */
/******************************************************************************/
