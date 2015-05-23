#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "tpool.h"

enum test_return { TEST_PASS, TEST_FAIL };
#define WORK_NUM 50

static void heavy_work(void *args)
{
	/* do some loops to simulate delay work */
	int i;
	for(i = 0; i < 20000; i++) {
		int j;
		for(j = 0; j < 2000; j++)
			;
	}
	return;
}

static enum test_return test_heavy_work(void) {
	int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
	void *tpool = tpool_init(cpu_num);;
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, heavy_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

static void light_work(void *args)
{
	/* return directly */
	return;
}

static enum test_return test_light_work(void) {
	int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
	void *tpool = tpool_init(cpu_num);
	int i;

	if (tpool == NULL)
		return TEST_FAIL;

	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, light_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

static enum test_return test_one_thread(void) {
	void *tpool = tpool_init(1);
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, heavy_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

static enum test_return test_tpool_destroy_directly(void) {
	int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
	void *tpool = tpool_init(cpu_num);
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, heavy_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 0);
	return TEST_PASS;
}

static enum test_return test_inc_thread(void) {
	void *tpool = tpool_init(5);
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	
	for(i = 0; i < WORK_NUM << 13; i++) {
		if (tpool_add_work(tpool, light_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	if (tpool_inc_threads(tpool, 5) < 0)
		return TEST_FAIL;
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

static enum test_return test_dec_thread(void) {
	void *tpool = tpool_init(12);
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, light_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_dec_threads(tpool, 6);
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, light_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

enum test_return test_least_load(void) {
	int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
	void *tpool = tpool_init(cpu_num);;
	int i;

	if (tpool == NULL)
		return TEST_FAIL;
	set_thread_schedule_algorithm(tpool, LEAST_LOAD);
	for(i = 0; i < WORK_NUM; i++) {
		if (tpool_add_work(tpool, heavy_work, NULL) < 0) {
			tpool_destroy(tpool, 0);
			return TEST_FAIL;
		}
	}
	tpool_destroy(tpool, 1);
	return TEST_PASS;
}

typedef enum test_return (*TEST_FUNC)(void);

struct testcase {
	const char *description;
	TEST_FUNC function;
};

struct testcase testcases[] = {
	{"one thread in thread pool", test_one_thread},
	{"heavy work", test_heavy_work},
	{"light work", test_light_work},
	{"drop remaing works and exit directly", test_tpool_destroy_directly},
	{"increase thread num", test_inc_thread},
	{"decrease thread num", test_dec_thread},
	{"set least load alogrithm", test_least_load},
	{ NULL, NULL }
};

int main()
{
	int exitcode = 0;
	int i = 0;
	struct timeval tstart,tend; 
	enum test_return ret;
	unsigned long timeuse;

	for (i = 0; testcases[i].description != NULL; ++i) {
		gettimeofday(&tstart,NULL); 
		ret = testcases[i].function();
		gettimeofday(&tend,NULL); 
		timeuse = 1000000 * (tend.tv_sec - tstart.tv_sec) + 
			tend.tv_usec - tstart.tv_usec; 
		if (ret == TEST_PASS) {
			printf("ok %d - %s    time: %luus\n", i + 1, testcases[i].description, timeuse);
		} else {
			printf("not ok %d - %s\n", i + 1, testcases[i].description);
			exitcode = 1;
		}
    }

    return exitcode;
}
