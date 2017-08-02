all: check_apache collectd_apache

check_apache: check_apache.o
	$(CC) $+ -o $@ `pkg-config --libs apr-1`

check_apache.o: check_apache.c
	$(CC) -c $< `apxs -q CFLAGS` -I`apxs -q INCLUDEDIR` `pkg-config --cflags apr-1`

collectd_apache: collectd_apache.o
	$(CC) $+ -o $@ `pkg-config --libs apr-1 libevent`

collectd_apache.o: collectd_apache.c
	$(CC) -c $< `apxs -q CFLAGS` -I`apxs -q INCLUDEDIR` `pkg-config --cflags apr-1`

clean:
	rm -f check_apache collectd_apache *.o
