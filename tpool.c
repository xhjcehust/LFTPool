/***************************************************************************
** Name         : tpool.c
** Author       : xhjcehust
** Version      : v1.0
** Date         : 2015-05
** Description  : Thread pool. 
**
** CSDN Blog    : http://blog.csdn.net/xhjcehust
** E-mail       : hjxiaohust@gmail.com
**
** This file may be redistributed under the terms
** of the GNU Public License.
***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include "tpool.h"

enum {
	TPOOL_ERROR,
	TPOOL_WARNING,
	TPOOL_DEBUG
};

#define debug(level, ...) do { \
	if (level < TPOOL_DEBUG) {\
		flockfile(stdout); \
		printf("###%p.%s: ", (void *)pthread_self(), __func__); \
		printf(__VA_ARGS__); \
		putchar('\n'); \
		fflush(stdout); \
		funlockfile(stdout);\
	}\
} while (0)

#define WORK_QUEUE_POWER 16
#define WORK_QUEUE_SIZE (1 << WORK_QUEUE_POWER)
#define WORK_QUEUE_MASK (WORK_QUEUE_SIZE - 1)
/* 
 * Just main thread can increase thread->in, we can make it safely.
 * However,  thread->out may be increased in both main thread and
 * worker thread during balancing thread load when new threads are added
 * to our thread pool...
*/
#define thread_out_val(thread)      (__sync_val_compare_and_swap(&(thread)->out, 0, 0))
#define thread_queue_len(thread)   ((thread)->in - thread_out_val(thread))
#define thread_queue_empty(thread) (thread_queue_len(thread) == 0)
#define thread_queue_full(thread)  (thread_queue_len(thread) == WORK_QUEUE_SIZE)
#define queue_offset(val)           ((val) & WORK_QUEUE_MASK)

/* enough large for any system */
#define MAX_THREAD_NUM  512

typedef struct tpool_work {
	void               (*routine)(void *);
	void                *arg;
	struct tpool_work   *next;
} tpool_work_t;

typedef struct {
	pthread_t    id;
	int          shutdown;
#ifdef DEBUG
	int          num_works_done;
#endif
	unsigned int in;		/* offset from start of work_queue where to put work next */
	unsigned int out;	/* offset from start of work_queue where to get work next */
	tpool_work_t work_queue[WORK_QUEUE_SIZE];
} thread_t;

typedef struct tpool tpool_t;
typedef thread_t* (*schedule_thread_func)(tpool_t *tpool);
struct tpool {
	int                 num_threads;
	thread_t            threads[MAX_THREAD_NUM];
	schedule_thread_func schedule_thread;
};

static pthread_t main_tid;
static volatile int global_num_thread = 0;

static int tpool_queue_empty(tpool_t *tpool)
{
	int i;

	for (i = 0; i < tpool->num_threads; i++)
		if (!thread_queue_empty(&tpool->threads[i]))
			return 0;
	return 1;
}

static thread_t* round_robin_schedule(tpool_t *tpool)
{
	static int cur_thread_index = -1;

	assert(tpool && tpool->num_threads > 0);
	cur_thread_index = (cur_thread_index + 1) % tpool->num_threads ;
	return &tpool->threads[cur_thread_index];
}

static thread_t* least_load_schedule(tpool_t *tpool)
{
	int i;
	int min_num_works_index = 0;

	assert(tpool && tpool->num_threads > 0);
	/* To avoid race, we adapt the simplest min value algorithm instead of min-heap */
	for (i = 1; i < tpool->num_threads; i++) {
		if (thread_queue_len(&tpool->threads[i]) < 
				thread_queue_len(&tpool->threads[min_num_works_index]))
				min_num_works_index = i;
	}
	return &tpool->threads[min_num_works_index];
}

static const schedule_thread_func schedule_alogrithms[] = {
	[ROUND_ROBIN] = round_robin_schedule,
	[LEAST_LOAD]  = least_load_schedule
};

void set_thread_schedule_algorithm(void *pool, enum schedule_type type)
{
	struct tpool *tpool = pool;

	assert(tpool);
	tpool->schedule_thread = schedule_alogrithms[type];
}

static void sig_do_nothing(int signo)
{
	return;
}

static tpool_work_t *get_work_concurrently(thread_t *thread)
{
	tpool_work_t *work = NULL;
	unsigned int tmp;

	do {
		work = NULL;
		if (thread_queue_len(thread) <= 0)
			break;
		tmp = thread->out;
		//prefetch work
		work = &thread->work_queue[queue_offset(tmp)];
	} while (!__sync_bool_compare_and_swap(&thread->out, tmp, tmp + 1));
	return work;
}

static void *tpool_thread(void *arg)
{
	thread_t *thread = arg;
	tpool_work_t *work = NULL;
	sigset_t zeromask, newmask, oldmask;

	/* SIGUSR1 handler has been set in tpool_init */
	__sync_fetch_and_add(&global_num_thread, 1);
	pthread_kill(main_tid, SIGUSR1);
	sigemptyset(&zeromask);
	sigemptyset(&newmask);
	sigaddset(&newmask, SIGUSR1);

	while (1) {
		if (sigprocmask(SIG_BLOCK, &newmask, &oldmask) < 0) {
			debug(TPOOL_ERROR, "SIG_BLOCK failed");
			pthread_exit(NULL);
		}
		while (thread_queue_empty(thread) && !thread->shutdown) {
			debug(TPOOL_DEBUG, "I'm sleep");
			sigsuspend(&zeromask);
		}
		if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0) {
			debug(TPOOL_ERROR, "SIG_SETMASK failed");
			pthread_exit(NULL);
		}
		debug(TPOOL_DEBUG, "I'm awake");
		
		if (thread->shutdown) {
			debug(TPOOL_DEBUG, "exit");
		#ifdef DEBUG
			printf("%ld: %d\n", thread->id, thread->num_works_done);
		#endif
			pthread_exit(NULL);
		}
		work = get_work_concurrently(thread);
		if (work) {
			(*(work->routine))(work->arg);
			#ifdef DEBUG
				thread->num_works_done++;
			#endif
		}
		if (thread_queue_empty(thread))
			pthread_kill(main_tid, SIGUSR1);
	}
}

static void spawn_new_thread(tpool_t *tpool, int index)
{
	memset(&tpool->threads[index], 0, sizeof(thread_t));
	if (pthread_create(&tpool->threads[index].id, NULL, tpool_thread, 
	            (void *)(&tpool->threads[index])) != 0) {
		debug(TPOOL_ERROR, "pthread_create failed");
		exit(0);
	}
}

static int wait_for_thread_registration(int num_expected)
{
	sigset_t zeromask, newmask, oldmask;

	sigemptyset(&zeromask);
	sigemptyset(&newmask);
	sigaddset(&newmask, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &newmask, &oldmask) < 0) {
		debug(TPOOL_ERROR, "SIG_BLOCK failed");
		return -1;
	}
	while (global_num_thread < num_expected)
		sigsuspend(&zeromask);
	if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0) {
		debug(TPOOL_ERROR, "SIG_SETMASK failed");
		return -1;
	}
	return 0;
}

void *tpool_init(int num_threads)
{
	int i;
	tpool_t *tpool;

	if (num_threads <= 0)
		return NULL;
	else if (num_threads > MAX_THREAD_NUM) {
		debug(TPOOL_ERROR, "too many threads!!!");
		return NULL;
	}
	tpool = malloc(sizeof(*tpool));
	if (tpool == NULL) {
		debug(TPOOL_ERROR, "malloc failed");
		return NULL;
	}

	memset(tpool, 0, sizeof(*tpool));
	tpool->num_threads = num_threads;
	tpool->schedule_thread = round_robin_schedule;
	/* all threads are set SIGUSR1 with sig_do_nothing */
	if (signal(SIGUSR1, sig_do_nothing) == SIG_ERR) {
		debug(TPOOL_ERROR, "signal failed");
		return NULL;
	}
	main_tid = pthread_self();
	for (i = 0; i < tpool->num_threads; i++)
		spawn_new_thread(tpool, i);
	if (wait_for_thread_registration(tpool->num_threads) < 0)
		pthread_exit(NULL);
	return (void *)tpool;
}

static int dispatch_work2thread(tpool_t *tpool, 
				thread_t *thread, void(*routine)(void *), void *arg)
{
	tpool_work_t *work = NULL;

	if (thread_queue_full(thread)) {
		debug(TPOOL_WARNING, "queue of thread selected is full!!!");
		return -1;
	}
	work = &thread->work_queue[queue_offset(thread->in)];
	work->routine = routine;
	work->arg = arg;
	work->next = NULL;
	thread->in++;
	if (thread_queue_len(thread) == 1) {
		debug(TPOOL_DEBUG, "signal has task");
		pthread_kill(thread->id, SIGUSR1);
	}
	return 0;
}

/* 
 * Here, worker threads died with work undone can not change from->out
 *  and we can read it directly...
*/
static int migrate_thread_work(tpool_t *tpool, thread_t *from)
{
	unsigned int i;
	tpool_work_t *work;
	thread_t *to;

	for (i = from->out; i < from->in; i++) {
		work = &from->work_queue[queue_offset(i)];
		to = tpool->schedule_thread(tpool);
		if (dispatch_work2thread(tpool, to, work->routine, work->arg) < 0)
			return -1;
	}
#ifdef DEBUG
	printf("%ld migrate_thread_work: %u\n", from->id, thread_queue_len(from));
#endif
	return 0;
}

static int isnegtive(int val)
{
	return val < 0;
}

static int ispositive(int val)
{
	return val > 0;
}

static int get_first_id(int arr[], int len, int (*fun)(int))
{
	int i;

	for (i = 0; i < len; i++)
		if (fun(arr[i]))
			return i;
	return -1;
}

/*
 * The load balance algorithm may not work so balanced because worker threads
 * are consuming work at the same time, which resulting in work count is not 
 * real-time
*/
static void balance_thread_load(tpool_t *tpool)
{
	int count[MAX_THREAD_NUM];
	int i, out, sum = 0, avg;
	int first_neg_id, first_pos_id, tmp, migrate_num;
	thread_t *from, *to;
	tpool_work_t *work;

	for (i = 0; i < tpool->num_threads; i++) {
		count[i] = thread_queue_len(&tpool->threads[i]);
		sum += count[i];
	}
	avg = sum / tpool->num_threads;
	if (avg == 0)
		return;
	for (i = 0; i < tpool->num_threads; i++)
		count[i] -= avg;
	while (1) {
		first_neg_id = get_first_id(count, tpool->num_threads, isnegtive);
		first_pos_id = get_first_id(count, tpool->num_threads, ispositive);
		if (first_neg_id < 0)
			break;
		tmp = count[first_neg_id] + count[first_pos_id];
		if (tmp > 0) {
			migrate_num = -count[first_neg_id];
			count[first_neg_id] = 0;
			count[first_pos_id] = tmp;
		} else {
			migrate_num = count[first_pos_id];
			count[first_pos_id] = 0;
			count[first_neg_id] = tmp;
		}
		from = &tpool->threads[first_pos_id];
		to = &tpool->threads[first_neg_id];
		for (i = 0; i < migrate_num; i++) {
			work = get_work_concurrently(from);
			if (work) {
				dispatch_work2thread(tpool, to, work->routine, work->arg);
			}
		}
	}
	from = &tpool->threads[first_pos_id];
	/* Just migrate count[first_pos_id] - 1 works to other threads*/
	for (i = 1; i < count[first_pos_id]; i++) {
		to = &tpool->threads[i - 1];
		if (to == from)
			continue;
		work = get_work_concurrently(from);
		if (work) {
			dispatch_work2thread(tpool, to, work->routine, work->arg);
		}
	}
}

int tpool_inc_threads(void *pool, int num_inc)
{
	tpool_t *tpool = pool;
	int i, num_threads;

	assert(tpool && num_inc > 0);
	num_threads = tpool->num_threads + num_inc;
	if (num_threads > MAX_THREAD_NUM) {
		debug(TPOOL_ERROR, "add too many threads!!!");
		return -1;
	}
	for (i = tpool->num_threads; i < num_threads; i++)
		spawn_new_thread(tpool, i);
	if (wait_for_thread_registration(num_threads) < 0)
		pthread_exit(NULL);
	tpool->num_threads = num_threads;
	balance_thread_load(tpool);
	return 0;
}

void tpool_dec_threads(void *pool, int num_dec)
{
	tpool_t *tpool = pool;
	int i, num_threads;

	assert(tpool && num_dec > 0);
	if (num_dec > tpool->num_threads)
		num_dec = tpool->num_threads;
	num_threads = tpool->num_threads;
	tpool->num_threads -= num_dec;
	for (i = tpool->num_threads; i < num_threads; i++) {
		tpool->threads[i].shutdown = 1;
		pthread_kill(tpool->threads[i].id, SIGUSR1);
	}
	for (i = tpool->num_threads; i < num_threads; i++) {
		pthread_join(tpool->threads[i].id, NULL);
		/* migrate remaining work to other threads */
		if (migrate_thread_work(tpool, &tpool->threads[i]) < 0)
			debug(TPOOL_WARNING, "work lost during migration!!!");
	}
	if (tpool->num_threads == 0 && !tpool_queue_empty(tpool))
		debug(TPOOL_WARNING, "No thread in pool with work unfinished!!!");
}

int tpool_add_work(void *pool, void(*routine)(void *), void *arg)
{
	tpool_t *tpool = pool;
	thread_t *thread;

	assert(tpool);
	thread = tpool->schedule_thread(tpool);
	return dispatch_work2thread(tpool, thread, routine, arg);
}

void tpool_destroy(void *pool, int finish)
{
	tpool_t *tpool = pool;
	int i;

	assert(tpool);
	if (finish == 1) {
		sigset_t zeromask, newmask, oldmask;

		debug(TPOOL_DEBUG, "wait all work done");
		/* SIGUSR1 handler has been set */
		sigemptyset(&zeromask);
		sigemptyset(&newmask);
		sigaddset(&newmask, SIGUSR1);
		if (sigprocmask(SIG_BLOCK, &newmask, &oldmask) < 0) {
			debug(TPOOL_ERROR, "SIG_BLOCK failed");
			pthread_exit(NULL);
		}
		while (!tpool_queue_empty(tpool))
			sigsuspend(&zeromask);
		if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0) {
			debug(TPOOL_ERROR, "SIG_SETMASK failed");
			pthread_exit(NULL);
		}
	}
	/* shutdown all threads */
	for (i = 0; i < tpool->num_threads; i++) {
		tpool->threads[i].shutdown = 1;
		/* wake up thread */
		pthread_kill(tpool->threads[i].id, SIGUSR1);
	}
	debug(TPOOL_DEBUG, "wait worker thread exit");
	for (i = 0; i < tpool->num_threads; i++)
		pthread_join(tpool->threads[i].id, NULL);
	free(tpool);
}
