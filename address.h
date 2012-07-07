/******************************************************************************/
/* address.h                                                                  */
/******************************************************************************/

#ifndef __ADDRESS_H__
#define __ADDRESS_H__

#include <sys/types.h>

#define MAX_ADDRESS_LENGTH 64

union toa {
	struct {
		unsigned char numbplan:4;
		unsigned char typenumb:3;
		unsigned char reserved:1;
	} __attribute__((packed)) bits;
	unsigned char full;
} __attribute__((packed));

struct address {
	union toa type;
	char value[MAX_ADDRESS_LENGTH];
	int length;
};

#define TYPE_OF_NUMBER_UNKNOWN			0
#define TYPE_OF_NUMBER_INTERNATIONAL		1
#define TYPE_OF_NUMBER_NATIONAL			2
#define TYPE_OF_NUMBER_NETWORK			3
#define TYPE_OF_NUMBER_SUBSCRIBER		4
#define TYPE_OF_NUMBER_ALPHANUMGSM7		5
#define TYPE_OF_NUMBER_ABBREVIATED		6
#define TYPE_OF_NUMBER_RESERVED			7

#define NUMBERING_PLAN_UNKNOWN			0x0
#define NUMBERING_PLAN_ISDN_E164			0x1
#define NUMBERING_PLAN_DATA_X121			0x3
#define NUMBERING_PLAN_TELEX				0x4
#define NUMBERING_PLAN_NATIONAL			0x8
#define NUMBERING_PLAN_PRIVATE			0x9
#define NUMBERING_PLAN_ERMES				0xA
#define NUMBERING_PLAN_RESERVED			0xF

//
int is_address_string(const char *buf);
void address_classify(const char *input, struct address *addr);
void address_normalize(struct address *addr);
char *address_show(char *buf, struct address *addr, int full);
int is_address_equal(struct address *a1, struct address *a2);

#endif //__ADDRESS_H__

/******************************************************************************/
/* end of address.h                                                           */
/******************************************************************************/
