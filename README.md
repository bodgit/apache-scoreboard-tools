nagios-apache-scoreboard
========================

Every man and his dog has written a Nagios Apache monitor that scrapes
the `mod _ status` _"/server-status/"_ URL to tell you how many workers
are in use or idle. However all of these suffer the same problem; because
you're using a TCP connection when Apache runs out of connections,
(because your application is bricked waiting for database connections,
etc.) your monitoring breaks/times out, etc. rather than say all of your
workers are busy.

An alternative idea is to monitor Apache "out-of-band" using the
scoreboard directly.

All you need is something like the following in your httpd.conf:

        ScoreboardFile /var/run/httpd/httpd.scoreboard

You don't need to have `mod _ status` enabled for this to work. Then
run the monitor like so:

        # ./check_apache -w 100 -c 200 /var/run/httpd/httpd.scoreboard
        WARNING: 150 workers in use

`-w` and `-c` refer to the warning and critical thresholds for the number
of workers _in use_.
