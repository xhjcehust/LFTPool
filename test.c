#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "thread_pool.h"

#define WORK_NUM 50

enum test_return {
    TEST_PASS,
    TEST_FAIL 
};

static void heavy_work(void *args) {
    int i;

    for(i = 0; i < 20000; i++) {
        int j;
        for(j = 0; j < 2000; j++) {
            ;
        }
    }

    return;
}

static enum test_return test_heavy_work(void) {
    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    void *thread_pool = thread_pool_init(cpu_num);;
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, heavy_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

static void light_work(void *args) {
    int i;

    for(i = 0; i < 20; i++) {
        int j;
        for(j = 0; j < 20; j++) {
            ;
        }
    }

    return;
}

static enum test_return test_light_work(void) {
    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    void *thread_pool = thread_pool_init(cpu_num);
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, light_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

static enum test_return test_one_thread(void) {
    void *thread_pool = thread_pool_init(1);
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, heavy_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

static enum test_return test_thread_pool_destroy_directly(void) {
    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    void *thread_pool = thread_pool_init(cpu_num);
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, heavy_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 0);

    return TEST_PASS;
}

static enum test_return test_inc_thread(void) {
    void *thread_pool = thread_pool_init(5);
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, light_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    if (thread_pool_inc_threads(thread_pool, 5) < 0) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, light_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

static enum test_return test_dec_thread(void) {
    void *thread_pool = thread_pool_init(10);
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, light_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_dec_threads(thread_pool, 5);

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, light_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

enum test_return test_least_load(void) {
    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    void *thread_pool = thread_pool_init(cpu_num);;
    int i;

    if (thread_pool == NULL) {
        return TEST_FAIL;
    }
        
    set_thread_schedule_algorithm(thread_pool, LEAST_LOAD);

    for(i = 0; i < WORK_NUM; i++) {
        if (thread_pool_add_work(thread_pool, heavy_work, NULL) < 0) {
            thread_pool_destroy(thread_pool, 0);
            return TEST_FAIL;
        }
    }

    thread_pool_destroy(thread_pool, 1);

    return TEST_PASS;
}

typedef enum test_return (*TEST_FUNC)(void);

struct testcase {
    const char *description;
    TEST_FUNC  function;
};

struct testcase testcases[] = {
    {"one thread in thread pool", test_one_thread},
    {"heavy work", test_heavy_work},
    {"light work", test_light_work},
    {"drop remaing works and exit directly", test_thread_pool_destroy_directly},
    {"increase thread num", test_inc_thread},
    {"decrease thread num", test_dec_thread},
    {"set least load alogrithm", test_least_load},
    { NULL, NULL }
};

int main(int argc, char* argv[]) {
    int exitcode = 0;
    int i = 0;
    struct timeval t_start, t_end;
    enum test_return ret;
    unsigned long time_use;

    printf("It may take you a few minutes to finish this test, please wait...\n");
    for (i = 0; testcases[i].description != NULL; ++i) {
        gettimeofday(&t_start, NULL);
        ret = testcases[i].function();
        gettimeofday(&t_end, NULL);
        time_use = 1000000 * (t_end.tv_sec - t_start.tv_sec) + t_end.tv_usec - t_start.tv_usec;
        if (ret == TEST_PASS) {
            printf("ok %d - %s    time: %luus\n", i + 1, testcases[i].description, time_use);
        } else {
            printf("not ok %d - %s\n", i + 1, testcases[i].description);
            exitcode = 1;
        }
    }

    return exitcode;
}
