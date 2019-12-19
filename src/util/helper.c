#include <math.h>
#include <sys/time.h>
#include <stdint.h>
#include "helper.h"

double tv_to_double(struct timeval *tv){
	double time_doub;
	time_doub =  tv->tv_sec * 1000000.0 + tv->tv_usec;
	return time_doub;
}

struct timeval *double_to_tv(double time_doub, struct timeval *tv){
	tv->tv_sec = (time_t)time_doub;
	tv->tv_usec = (uint64_t)(fmod(time_doub, 1) * 1000000);
	return tv;
}
