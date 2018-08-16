#ifndef _WLSS_H
#define _WLSS_H
#include <glib.h>

struct __data_st {
	guint sid;
	guint ts;
	guint volt;
	guint rssi;
	
	guint type;
	guint channel;
	guint freq;
	guint nums;
	
	guint alen;
	guint aflag;
	gfloat temp;
	gfloat p_val;
	
	gfloat pp_val;
	gfloat rms_val;
	guint rev1;
	guint rev2;
	
	gfloat data[1];
};
enum __CH{
	CH_ACC = 0x01,
	CH_VEL = 0x02,
	CH_HAC = 0x08,
	CH_USR = 0x40,
	CH_ALM = 0X80
};

#endif
