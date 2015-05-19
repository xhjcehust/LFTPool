#ifndef __TPOOL_H__
#define __TPOOL_H__

enum schedule_type {
	ROUND_ROBIN,
	LEAST_LOAD
};

void *tpool_init(int num_worker_threads);

int tpool_inc_threads(void *pool, int num_inc);

void tpool_dec_threads(void *pool, int num_dec);

int tpool_add_work(void *pool, void(*routine)(void *), void *arg);
/* 
@finish:  1, complete remaining works before return
		0, drop remaining works and return directly
*/
void tpool_destroy(void *pool, int finish);

/* set thread schedule algorithm, default is round-robin */
void set_thread_schedule_algorithm(void *pool, enum schedule_type type);

#endif
