/******************************************************************************/
/* x_timer.h                                                                  */
/******************************************************************************/

#ifndef __X_TIMER_H__
#define __X_TIMER_H__

#include <sys/types.h>

struct x_timer {
	int enable;
	struct timeval start;
	struct timeval timeout;
	struct timeval expires;
};

#define tv_set(_tv, _sec, _usec) \
	do { \
		_tv.tv_sec = _sec; \
		_tv.tv_usec = _usec; \
	} while(0)

#define tv_cpy(_dest, _src) \
	do { \
		_dest.tv_sec = _src.tv_sec; \
		_dest.tv_usec = _src.tv_usec; \
	} while(0)

#define tv_add(_tv_1, _tv_2) \
	do { \
		_tv_1.tv_sec += _tv_2.tv_sec; \
		_tv_1.tv_usec += _tv_2.tv_usec; \
		if (_tv_1.tv_usec >= 1000000) { \
			_tv_1.tv_usec -= 1000000; \
			_tv_1.tv_sec += 1; \
		} \
	} while(0)

#define tv_cmp(tv_1, tv_2) \
	({ \
		int res = 0; \
		if ((tv_1)->tv_sec < (tv_2)->tv_sec) \
			res = -1; \
		else if ((tv_1)->tv_sec > (tv_2)->tv_sec) \
			res = 1; \
		else if ((tv_1)->tv_sec == (tv_2)->tv_sec) { \
			if ((tv_1)->tv_usec < (tv_2)->tv_usec) \
				res = -1; \
			else if ((tv_1)->tv_usec > (tv_2)->tv_usec) \
				res = 1; \
			else \
				res = 0; \
		} \
		res; \
	})

#define x_timer_set(_timer, _timeout) \
	do { \
		struct timeval __curr_time; \
		gettimeofday(&__curr_time, NULL); \
		_timer.enable = 1; \
		tv_cpy(_timer.start, __curr_time); \
		tv_cpy(_timer.timeout, _timeout); \
		tv_cpy(_timer.expires, _timer.start); \
		tv_add(_timer.expires, _timer.timeout); \
	} while(0)

#define x_timer_stop(_timer) \
	do { \
		_timer.enable = 0; \
	} while(0)

#define is_x_timer_enable(_timer) \
	({ \
		int _res = 0; \
		_res = _timer.enable; \
		_res; \
	})

#define is_x_timer_active(_timer) \
	({ \
		int _res = 0; \
		struct timeval __curr_time; \
		gettimeofday(&__curr_time, NULL); \
		if (tv_cmp(&(_timer.expires), &(__curr_time)) > 0) \
			_res = 1; \
		else \
			_res = 0; \
		_res; \
	})

#define is_x_timer_fired(_timer) \
	({ \
		int _res = 0; \
		struct timeval __curr_time; \
		gettimeofday(&__curr_time, NULL); \
		if (tv_cmp(&(_timer.expires), &(__curr_time)) > 0) \
			_res = 0; \
		else \
			_res = 1; \
		_res; \
	})

#endif //__X_TIMER_H__

/******************************************************************************/
/* end of x_timer.h                                                           */
/******************************************************************************/
