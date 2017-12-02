testtpool:test.c thread_pool.c
	gcc -o testtpool -g test.c thread_pool.c -lpthread
debug-testtpool:test.c thread_pool.c
	gcc -o debug-testtpool -g test.c thread_pool.c -lpthread -DDEBUG
.PHONY:clean
clean:
	-rm -f testtpool debug-testtpool
