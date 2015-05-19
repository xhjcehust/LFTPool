testtpool:test.c tpool.c tpool.h
	gcc -o testtpool -g test.c tpool.c -lpthread
debug-testtpool:test.c tpool.c tpool.h
	gcc -o debug-testtpool -g test.c tpool.c -lpthread -DDEBUG
.PHONY:clean
clean:
	-rm -f testtpool debug-testtpool
