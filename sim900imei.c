/******************************************************************************/
/* sim900imei.c                                                               */
/******************************************************************************/

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <ctype.h>
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

#include "imei.h"
#include "strutil.h"
#include "x_timer.h"

static const unsigned char sim900_set_storage_equipment[9] = {
	0x04,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x00, 0x00,
};

static const unsigned char sim900_set_storage_equipment_s1[9] = {
	0x04,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00};


static const unsigned char sim900_configuration_for_erased_area[9] = {
	0x09,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x7f, 0x00,
};

static const const unsigned char sim900_configuration_for_erased_area_s0[9] = {
	0x09,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00};

static const unsigned char sim900_configuration_for_erased_area_s1[9] = {
	0x09,
	0x00, 0x00, 0x51, 0x90,
	0x00, 0x00, 0x2e, 0x00};

static const unsigned char sim900_set_for_downloaded_code_information[9] = {
	0x04,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x00, 0x00,
};

static const unsigned char sim900_set_for_downloaded_code_information_s0[9] = {
	0x04,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00,
};

static const unsigned char sim900_set_for_downloaded_code_section[5] = {
	0x01,
	0x00, 0x08, 0x00, 0x00,
};

static const unsigned char sim900_comparision_for_downloaded_information[13] = {
	0x15,
	0x00, 0x00, 0x00, 0x90,
	0xE7, 0xDA, 0x45, 0x0D,
	0x00, 0x00, 0x00, 0x00,
};

static const char *sim900imei_usage = "Usage: sim900imei -d <device> -i imei [-h <File of Intel HEX>]\n";

static const char *intel_hex_default = "flash_nor_16bits_hwasic_evp_4902_rel.hex";

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

int main(int argc, char **argv)
{
	int opt;

	char *device = NULL;
	char *hex = NULL;
	char *imei = NULL;

	char sim900_code_page[0x10000];

	size_t i;

	char t_buf[1024];
	char *t_ptr;
	size_t t_pos;
	ssize_t t_size;
	size_t t_total;

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
	struct termios tty_termios, old_termios;

	char hex_fpath[PATH_MAX];
	FILE *hex_fptr = NULL;

	struct x_timer timer;
	char t_char;
	int res;

	struct timeval timeout;
	fd_set fds;

	while ((opt = getopt(argc, argv, "d:h:i:")) != -1)
	{
		switch (opt)
		{
			case 'd': /*! - device, for example "board-k32pci-pci-234-gsm2" */
				device = optarg;
				break;
			case 'h': /*! - File of Intel HEX, default "flash_nor_16bits_hwasic_evp_4902_rel.hex" */
				hex = optarg;
				break;
			case 'i':
				imei = optarg;
				break;
			default: /*! '?' */
				printf(sim900imei_usage);
				goto main_end;
		}
	}

	if (!device) {
		printf("device not specified\n");
		printf(sim900imei_usage);
		goto main_end;
	}

	if (!imei) {
		printf("IMEI not present\n");
		printf(sim900imei_usage);
		goto main_end;
	}

	if ((res = imei_is_valid(imei)) != EIMEI_VALID) {
		printf("IMEI=\"%s\" invalid: %s - ", imei, imei_strerror(-res));
		goto main_end;
	}
	printf("IMEI=\"%.*s(%c)\"\n", 14, imei, (char)imei_calc_check_digit(imei));

	if (hex)
		snprintf(hex_fpath, sizeof(hex_fpath), "%s", hex);
	else
		snprintf(hex_fpath, sizeof(hex_fpath), "%s/polygator/%s", ASTERISK_DATA_DEFAULT_PATH, intel_hex_default);

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
		goto main_end;
	}

	printf("Starting IMEI SIM900 change\n");

	// open TTY device
	if ((tty_fd = open(tty_fpath, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
		printf("can't open \"%s\": %s\n", tty_fpath, strerror(errno));
		goto main_end;
	}
	// get termios
	if (tcgetattr(tty_fd, &old_termios)) {
		printf("tcgetattr() error: %s\n", strerror(errno));
		goto main_end;
	}
	if (tcgetattr(tty_fd, &tty_termios)) {
		printf("tcgetattr() error: %s\n", strerror(errno));
		goto main_end;
	}
	cfmakeraw(&tty_termios);
	tty_termios.c_cc[VMIN] = 0;
	tty_termios.c_cc[VTIME] = 0;
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

	if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 0) < 0) {
		printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
		goto main_end;
	}
	if (pg_channel_gsm_power_set(board_fpath, pos_on_board, 0) < 0) {
		printf("pg_channel_gsm_power_set() error: %s\n", strerror(errno));
		goto main_end;
	}
	sleep(1);
	printf("Turn GSM module power to ON...");
	if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 1) < 0) {
		printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
		goto main_end;
	}
	if (pg_channel_gsm_power_set(board_fpath, pos_on_board, 1) < 0) {
		printf("pg_channel_gsm_power_set() error: %s\n", strerror(errno));
		goto main_end;
	}
	printf("ok\n");
	fflush(stdout);

	// Detection of synchronous bytes
	printf("Detection of synchronous bytes...");
	fflush(stdout);

	x_timer_set_second(timer, 10);
	while (is_x_timer_active(timer))
	{
		// writing synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				t_char = 0x16;
				res = write(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				}
			}
		} if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
		// wait for synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				// read synchronous octet
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
// 					printf("read 0x%02x - %c\n", (unsigned char)t_char, isprint(t_char)?t_char:'?');
					if (t_char == 0x16)
						break;	// synchronous octet received
				}
			} if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
	}
	// check for entering into downloading procedure
	if (is_x_timer_fired(timer)) {
		printf("run in normal mode - quit from program\n");
		goto main_end;
	}
// 	if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 0) < 0) {
// 		printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
// 		goto main_end;
// 	}
	if (tcflush(tty_fd, TCIOFLUSH) < 0)
		printf("can't flush tty device: %s\n", strerror(errno));
	// entering into downloading procedure
	printf("module entered into downloading procedure\n");

	// File of Intel HEX download
	printf("File of Intel HEX download...");
	fflush(stdout);
	if (!(hex_fptr = fopen(hex_fpath, "r"))) {
		printf("failed - fopen(%s): %s\n", hex_fpath, strerror(errno));
		goto main_end;
	}
	// write hex file into device
	t_total = 0;
	while (fgets(t_buf, sizeof(t_buf), hex_fptr))
	{
		t_size = strlen(t_buf);
		t_pos = 0;
		x_timer_set_second(timer, 5);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 0;
			timeout.tv_usec = (t_size - t_pos) * 1000;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = write(tty_fd, &t_buf[t_pos], t_size - t_pos);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - write(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res > 0) {
						t_pos += res;
						if (t_size == t_pos) {
							printf(".");
							fflush(stdout);
							t_total += t_size;
							break;
						}
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}
	}
	// wait for success download of Intel HEX indication
	x_timer_set_second(timer, 5);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x00) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	fclose(hex_fptr);
	hex_fptr = NULL;
	printf("succeeded - total %lu bytes\n", (unsigned long int)t_total);

	// Set the storage equipment
	printf("Set the storage equipment...");
	fflush(stdout);

	t_ptr = (char *)&sim900_set_storage_equipment;
	t_size = sizeof(sim900_set_storage_equipment);
	t_pos = 0;
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// wait for success indication
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x04) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	// Read the flash manufacturer information
	printf("Read the flash manufacturer information...");
	fflush(stdout);

	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		// writing synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				t_char = 0x02;
				res = write(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} if (res == 1) break;
			}
		} if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - write - timeout\n");
		goto main_end;
	}
	// read marker
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x02) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// read data
	for (i=0; i<4; i++)
	{
		x_timer_set_second(timer, 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = read(tty_fd, &t_char, 1);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - read(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res == 1) break;
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}
	}
	printf("succeeded\n");

	// Set the storage equipment
	printf("Set the storage equipment...");
	fflush(stdout);

	t_ptr = (char *)&sim900_set_storage_equipment_s1;
	t_size = sizeof(sim900_set_storage_equipment_s1);
	t_pos = 0;
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// wait for success indication
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x04) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	// Read the code page
	printf("Read the code page...");
	fflush(stdout);

	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		// writing synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				t_char = 0x17;
				res = write(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} if (res == 1) break;
			}
		} if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - write - timeout\n");
		goto main_end;
	}
	// read marker
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x17) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// read data
	t_ptr = (char *)&sim900_code_page;
	t_size = sizeof(sim900_code_page);
	t_pos = 0;
	x_timer_set_second(timer, t_size/1000 + 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &sim900_code_page[t_pos], t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
			}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	// Set new IMEI into specified code page position
	str_digit_to_bcd(imei, 14, t_buf);
	t_char = (char)imei_calc_check_digit(imei);
	str_digit_to_bcd(&t_char, 1, t_buf+7);
	memcpy(&sim900_code_page[0xA568], t_buf, 8);

	// Configuration for erased area of FLASH
	printf("Configuration for erased area of FLASH...");
	fflush(stdout);

	t_ptr = (char *)&sim900_configuration_for_erased_area_s0;
	t_size = sizeof(sim900_configuration_for_erased_area_s0);
	t_pos = 0;
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// wait for success indication
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x09) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	// FLASH erase
	printf("FLASH erase");
	fflush(stdout);

	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		// writing synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				t_char = 0x03;
				res = write(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} if (res == 1) break;
			}
		} if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - write - timeout\n");
		goto main_end;
	}
	// read marker
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x03) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// Wait for flash erase
	t_total = 0;
	x_timer_set_second(timer, 300);
	while(is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x30) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		} else {
			printf(".");
			fflush(stdout);
			t_total++;
		}
	}
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - timeout\n");
		goto main_end;
	}
	printf("succeeded - in %lu seconds\n", (unsigned long int)t_total);

	// Set for downloaded code information
	printf("Set for downloaded code information...");
	fflush(stdout);

	t_ptr = (char *)&sim900_set_for_downloaded_code_information_s0;
	t_size = sizeof(sim900_set_for_downloaded_code_information_s0);
	t_pos = 0;
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// wait for success indication
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x04) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	printf("Code page download...");
	fflush(stdout);

	for (i=0; i<32; i++)
	{
		// Set for downloaded code section
		t_ptr = (char *)&sim900_set_for_downloaded_code_section;
		t_size = sizeof(sim900_set_for_downloaded_code_section);
		t_pos = 0;
		x_timer_set_second(timer, 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 0;
			timeout.tv_usec = (t_size - t_pos) * 1000;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - write(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res > 0) {
						t_pos += res;
						if (t_size == t_pos) break;
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}
		// read marker
		x_timer_set_second(timer, 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = read(tty_fd, &t_char, 1);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - read(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res == 1) {
						if (t_char == 0x01) break;
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}

		// Download code data
		t_ptr = (char *)&sim900_code_page[i*0x800];
		t_size = 0x800;
		t_pos = 0;
		x_timer_set_second(timer, t_size/1000 + 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 0;
			timeout.tv_usec = (t_size - t_pos) * 1000;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - write(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res > 0) {
						t_pos += res;
						if (t_size == t_pos) break;
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}
		// read marker 0x2e
		x_timer_set_second(timer, 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = read(tty_fd, &t_char, 1);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - read(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res == 1) {
						if (t_char == 0x2e) break;
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}
		// read marker 0x30
		x_timer_set_second(timer, 1);
		while (is_x_timer_active(timer))
		{
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(tty_fd, &fds);
			res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
			if (res > 0) {
				if (FD_ISSET(tty_fd, &fds)) {
					res = read(tty_fd, &t_char, 1);
					if (res < 0) {
						if (errno != EAGAIN) {
							printf("failed - read(): %s\n", strerror(errno));
							goto main_end;
						}
					} else if (res == 1) {
						if (t_char == 0x30) break;
					}
				}
			} else if (res < 0) {
				printf("failed - select(): %s\n", strerror(errno));
				goto main_end;
			}
		}
		if (is_x_timer_fired(timer)) {
			printf("failed - time is out\n");
			goto main_end;
		}

		printf(".");
		fflush(stdout);
	}
	printf("succeeded\n");

	// Configuration for erased area of FLASH
	printf("Configuration for erased area of FLASH...");
	fflush(stdout);

	t_ptr = (char *)&sim900_configuration_for_erased_area_s1;
	t_size = sizeof(sim900_configuration_for_erased_area_s1);
	t_pos = 0;
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = (t_size - t_pos) * 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = write(tty_fd, t_ptr + t_pos, t_size - t_pos);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res > 0) {
					t_pos += res;
					if (t_size == t_pos) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// wait for success indication
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x09) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	printf("succeeded\n");

	// FLASH erase
	printf("FLASH erase...");
	fflush(stdout);

	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		// writing synchronous octet
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, NULL, &fds, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				t_char = 0x03;
				res = write(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - write(): %s\n", strerror(errno));
						goto main_end;
					}
				} if (res == 1) break;
			}
		} if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - write - timeout\n");
		goto main_end;
	}
	// read marker
	x_timer_set_second(timer, 1);
	while (is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x03) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		}
	}
	if (is_x_timer_fired(timer)) {
		printf("failed - time is out\n");
		goto main_end;
	}
	// Wait for flash erase
	t_total = 0;
	x_timer_set_second(timer, 300);
	while(is_x_timer_active(timer))
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(tty_fd, &fds);
		res = select(tty_fd + 1, &fds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(tty_fd, &fds)) {
				res = read(tty_fd, &t_char, 1);
				if (res < 0) {
					if (errno != EAGAIN) {
						printf("failed - read(): %s\n", strerror(errno));
						goto main_end;
					}
				} else if (res == 1) {
					if (t_char == 0x30) break;
				}
			}
		} else if (res < 0) {
			printf("failed - select(): %s\n", strerror(errno));
			goto main_end;
		} else {
			printf(".");
			fflush(stdout);
			t_total++;
		}
	}
	// check for completion
	if (is_x_timer_fired(timer)) {
		printf("failed - timeout\n");
		goto main_end;
	}
	printf("succeeded - in %lu seconds\n", (unsigned long int)t_total);

	printf("IMEI write: completed\n");

main_end:
	if (hex_fptr) fclose(hex_fptr);
	if (tty_fd > -1) {
		// restore termios
		if (tcsetattr(tty_fd, TCSANOW, &old_termios) < 0)
			printf("tcsetattr() error: %s\n", strerror(errno));
		// close TTY device
		close(tty_fd);
		// disable GSM module
		if (pg_channel_gsm_key_press(board_fpath, pos_on_board, 0) < 0) {
			printf("pg_channel_gsm_key_press() error: %s\n", strerror(errno));
			goto main_end;
		}
		if (pg_channel_gsm_power_set(board_fpath, pos_on_board, 0) < 0) {
			printf("pg_channel_gsm_power_set() error: %s\n", strerror(errno));
			goto main_end;
		}
	}
	exit(EXIT_SUCCESS);
}

/******************************************************************************/
/* end of sim900imei.c                                                        */
/******************************************************************************/
