#include <stdio.h>
#include <unistd.h>

#include "httpd.h"
#include "scoreboard.h"

extern char	*__progname;
static char	*status[4] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };

int main(int argc, char *argv[])
{
	apr_status_t	 rv;
	apr_pool_t	*pool;
	apr_shm_t	*shm;
	void		*p;
	size_t		 size;
	global_score	*global;
	process_score	*parent;
	worker_score	*server;
	int		 c, i, j, used;
	int		 critical, warning, rc;
	char		 errbuf[256];

	critical = warning = 0;

	while ((c = getopt(argc, argv, "c:w:")) != -1) {
		switch (c) {
		case 'c':
			critical = atoi(optarg);
			break;
		case 'w':
			warning = atoi(optarg);
			break;
		default:
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		printf("%s: no scoreboard\n", status[3]);
		return (3);
	}

	apr_initialize();

	if ((rv = apr_pool_create(&pool, NULL)) != APR_SUCCESS) {
		printf("%s: apr_pool_create() - %s\n", status[3],
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		return (3);
	}

	if ((rv = apr_shm_attach(&shm, argv[0], pool)) != APR_SUCCESS) {
		printf("%s: apr_shm_attach() - %s\n", status[3],
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		return (3);
	}

	if (!(p = apr_shm_baseaddr_get(shm))) {
		printf("%s: apr_shm_baseaddr_get() - failed\n", status[3]);
		return (3);
	}

	if ((size = apr_shm_size_get(shm)) < sizeof(*global)) {
		printf("%s: apr_shm_size_get() - too small\n", status[3]);
		return (3);
	}

	global = p;
	p += sizeof(*global);

	parent = p;
	p += sizeof(*parent) * global->server_limit;

	/* Count how many are in use */
	used = 0;

	for (i = 0; i < global->server_limit; i++) {
		if (!parent[i].pid) continue;
		for (j = 0; j < global->thread_limit; j++) {
			server = p;
			p += sizeof(*server);
			/* Ignore any dead/ready slots and anything with an
			 * illegal status code (shouldn't happen)
			 */
			if ((server->status == SERVER_DEAD)
			    || (server->status == SERVER_READY)
			    || (server->status >= SERVER_NUM_STATUS))
				continue;
			/* Busy slot, increment in use counter */
			used++;
		}
	}

	if ((rv = apr_shm_detach(shm)) != APR_SUCCESS) {
		printf("%s: apr_shm_detach() - %s\n", status[3],
		    apr_strerror(rv, errbuf, sizeof(errbuf)));
		return (3);
	}

	rc = 0;

	if (warning && used >= warning) {
		rc = 1;
	}
	if (critical && used >= critical) {
		rc = 2;
	}

	printf("%s: %d worker%s in use | RunningWorkers=%d;%d;%d;;\n", status[rc], used,
	    (used != 1) ? "s" : "",
	    used, warning, critical);

	return rc;
}
