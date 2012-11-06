/******************************************************************************/
/* x_timer.h                                                                  */
/******************************************************************************/

#ifndef __X_TIMER_H__
#define __X_TIMER_H__

#include <sys/time.h>
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

#define tv_cmp(_tv_1, _tv_2) \
({ \
	int __res = 0; \
	if (_tv_1.tv_sec < _tv_2.tv_sec) \
		__res = -1; \
	else if (_tv_1.tv_sec > _tv_2.tv_sec) \
		__res = 1; \
	else if (_tv_1.tv_sec == _tv_2.tv_sec) { \
		if (_tv_1.tv_usec < _tv_2.tv_usec) \
			__res = -1; \
		else if (_tv_1.tv_usec > _tv_2.tv_usec) \
			__res = 1; \
		else \
			__res = 0; \
	} \
	__res; \
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

#define x_timer_set_second(_timer, _timeout) \
do { \
	struct timeval __curr_time; \
	struct timeval __timeout; \
	tv_set(__timeout, _timeout, 0); \
	gettimeofday(&__curr_time, NULL); \
	_timer.enable = 1; \
	tv_cpy(_timer.start, __curr_time); \
	tv_cpy(_timer.timeout, __timeout); \
	tv_cpy(_timer.expires, _timer.start); \
	tv_add(_timer.expires, _timer.timeout); \
} while(0)

#define x_timer_stop(_timer) \
do { \
	_timer.enable = 0; \
} while(0)

#define is_x_timer_enable(_timer) \
({ \
	int __res = 0; \
	__res = _timer.enable; \
	__res; \
})

#define is_x_timer_active(_timer) \
({ \
	int __res = 0; \
	struct timeval __curr_time; \
	gettimeofday(&__curr_time, NULL); \
	if ((tv_cmp(_timer.expires, __curr_time) > 0) && (tv_cmp(_timer.start, __curr_time) < 0)) \
		__res = 1; \
	else \
		__res = 0; \
	__res; \
})

#define is_x_timer_fired(_timer) \
({ \
	int __res = 0; \
	struct timeval __curr_time; \
	gettimeofday(&__curr_time, NULL); \
	if ((tv_cmp(_timer.expires, __curr_time) > 0) && (tv_cmp(_timer.start, __curr_time) < 0)) \
		__res = 0; \
	else \
		__res = 1; \
	__res; \
})

#endif //__X_TIMER_H__

/******************************************************************************/
/* end of x_timer.h                                                           */
/******************************************************************************/
