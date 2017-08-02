#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "httpd.h"
#include "scoreboard.h"

struct event_base	*base;
struct event		*connect_ev, *apache_ev;
struct bufferevent	*bev;
struct timeval		 interval = { 60, 0 };
struct timeval		 now = { 0, 0 };
apr_pool_t		*pool;
char			*fqdn;
char			*path;

const char		*label[SERVER_NUM_STATUS] = {
	"open",
	"starting",
	"waiting",
	"reading",
	"sending",
	"keepalive",
	"logging",
	"dnslookup",
	"closing",
	"finishing",
	"idle_cleanup",
};

void
collectd_read(struct bufferevent *bev, void *arg)
{
	struct evbuffer	*input = bufferevent_get_input(bev);

	/* Just drain any input away */
	evbuffer_drain(input, evbuffer_get_length(input));
}

void
apache_timer(int fd, short event, void *arg)
{
	apr_status_t	 rv;
	apr_shm_t	*shm;
	void		*p;
	size_t		 size;
	global_score	*global;
	process_score	*parent;
	worker_score	*server;
	int		 i, j, used[SERVER_NUM_STATUS];
	char		 errbuf[256];
	long		 accesses;
	long long	 bytes;
	int		 busy, ready;

	if ((rv = apr_shm_attach(&shm, path, pool)) != APR_SUCCESS) {
		fprintf(stderr, "apr_shm_attach() - %s\n",
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		goto retry;
	}

	if (!(p = apr_shm_baseaddr_get(shm))) {
		fprintf(stderr, "apr_shm_baseaddr_get() - failed\n");
		goto retry;
	}

	if ((size = apr_shm_size_get(shm)) < sizeof(*global)) {
		fprintf(stderr, "apr_shm_size_get() - too small\n");
		goto retry;
	}

	global = p;
	p += sizeof(*global);

	parent = p;
	p += sizeof(*parent) * global->server_limit;

	/* Zero the counters */
	accesses = 0;
	busy = 0;
	bytes = 0;
	ready = 0;
	memset(used, 0, sizeof(used));

	for (i = 0; i < global->server_limit; i++) {
		for (j = 0; j < global->thread_limit; j++) {
			server = p;
			p += sizeof(*server);

			if (parent[i].pid)
				switch (server->status) {
				case SERVER_DEAD:
				case SERVER_STARTING:
				case SERVER_IDLE_KILL:
					break;
				case SERVER_READY:
					ready++;
					break;
				default:
					busy++;
					break;
				}

			/* Increment in use counter */
			if (server->status < SERVER_NUM_STATUS)
				used[server->status]++;

			if (server->access_count > 0 ||
			    (server->status != SERVER_READY &&
			     server->status != SERVER_DEAD)) {
				accesses += server->access_count;
				bytes += server->bytes_served;
			}
		}
	}

	if ((rv = apr_shm_detach(shm)) != APR_SUCCESS) {
		fprintf(stderr, "apr_shm_detach() - %s\n",
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		goto retry;
	}

	evbuffer_add_printf(bufferevent_get_output(bev),
	    "PUTVAL \"%s/apache/apache_bytes\" interval=%ld N:%lld\n",
	    fqdn, interval.tv_sec, bytes);
	evbuffer_add_printf(bufferevent_get_output(bev),
	    "PUTVAL \"%s/apache/apache_requests\" interval=%ld N:%ld\n",
	    fqdn, interval.tv_sec, accesses);
	evbuffer_add_printf(bufferevent_get_output(bev),
	    "PUTVAL \"%s/apache/apache_connections\" interval=%ld N:%d\n",
	    fqdn, interval.tv_sec, busy);
	evbuffer_add_printf(bufferevent_get_output(bev),
	    "PUTVAL \"%s/apache/apache_idle_workers\" interval=%ld N:%d\n",
	    fqdn, interval.tv_sec, ready);
	for (i = 0; i < SERVER_NUM_STATUS; i++)
		evbuffer_add_printf(bufferevent_get_output(bev),
		    "PUTVAL \"%s/apache/apache_scoreboard-%s\" interval=%ld N:%d\n",
		    fqdn, label[i], interval.tv_sec, used[i]);

retry:
	evtimer_add(apache_ev, &interval);
}

void
collectd_event(struct bufferevent *bev, short events, void *arg)
{
	if (events & BEV_EVENT_CONNECTED) {
		apache_ev = evtimer_new(base, apache_timer, NULL);
		evtimer_add(apache_ev, &now);
	} else if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (events & BEV_EVENT_EOF) {
			evtimer_del(apache_ev);
			event_free(apache_ev);
		}

		bufferevent_free(bev);
		evtimer_add(connect_ev, &now);
	}
}

void
collectd_connect(int fd, short event, void *arg)
{
	char			*socket = (char *)arg;
	struct sockaddr_un	 addr;
	struct timeval		 tv = { 1, 0 };

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket, sizeof(addr.sun_path)-1);

	bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);

	if (bufferevent_socket_connect(bev, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		bufferevent_free(bev);
		evtimer_add(connect_ev, &tv);
		return;
	}

	bufferevent_setcb(bev, collectd_read, NULL, collectd_event, NULL);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

int
main(int argc, char *argv[])
{
	char		 hostname[HOST_NAME_MAX+1];
	struct addrinfo	*info, *ptr;
	struct addrinfo	 hints;
	int		 gai_result;
	int		 c;
	apr_status_t	 rv;
	char		 errbuf[256];
	char		*env;

	if ((env = getenv("COLLECTD_INTERVAL")) != NULL)
		interval.tv_sec = atoi(env);

	if ((env = getenv("COLLECTD_HOSTNAME")) == NULL) {
		gethostname(hostname, HOST_NAME_MAX);

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_CANONNAME;

		if ((gai_result = getaddrinfo(hostname, NULL, &hints, &info)) != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_result));
			return (1);
		}

		for (ptr = info; ptr != NULL; ptr = ptr->ai_next)
			fqdn = strdup(ptr->ai_canonname);

		freeaddrinfo(info);
	} else
		fqdn = strdup(env);

	while ((c = getopt(argc, argv, "i:h:")) != -1) {
		switch (c) {
		case 'i':
			interval.tv_sec = atoi(optarg);
			break;
		case 'h':
			free(fqdn);
			fqdn = strdup(optarg);
			break;
		default:
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, "No scoreboard\n");
		return (1);
	}

	path = argv[0];

	apr_initialize();

	if ((rv = apr_pool_create(&pool, NULL)) != APR_SUCCESS) {
		fprintf(stderr, "apr_pool_create() - %s\n",
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		return (1);
	}

	base = event_base_new();

	if (argc > 1) {
		connect_ev = evtimer_new(base, collectd_connect, (void *)argv[1]);
		evtimer_add(connect_ev, &now);
	} else {
		bev = bufferevent_socket_new(base, STDIN_FILENO, BEV_OPT_CLOSE_ON_FREE);
		bufferevent_setcb(bev, collectd_read, NULL, NULL, NULL);
		bufferevent_enable(bev, EV_READ);

		bev = bufferevent_socket_new(base, STDOUT_FILENO, BEV_OPT_CLOSE_ON_FREE);
		bufferevent_enable(bev, EV_WRITE);

		apache_ev = evtimer_new(base, apache_timer, NULL);
		evtimer_add(apache_ev, &now);
	}

	event_base_dispatch(base);

	return (0);
}
