apache-scoreboard-tools
=======================

Every man and his dog has written tools to monitor Apache by scraping the
`mod_status` _"/server-status/"_ URL to tell you how many workers are in use or
idle. However all of these tools suffer the same problem; because you're using
one of the available TCP connections, when Apache happens to run out of them,
(because your application is bricked waiting for database connections, etc.)
your monitoring breaks/times out, etc. rather than correctly report that all of
your workers are busy.

An alternative idea is to monitor Apache "out-of-band" using the scoreboard
directly.

All you need is something like the following in your httpd.conf:

        ScoreboardFile /var/run/httpd/httpd.scoreboard

You need `mod_status` enabled for this to work and also `ExtendedStatus` set to
`on` (the default since 2.3.6) but you don't need to expose any of this with a
`<Location>` block and `SetHandler` directive.

There is a Nagios-style check you can run like so:

        # ./check_apache -w 100 -c 200 /var/run/httpd/httpd.scoreboard
        WARNING: 150 workers in use

`-w` and `-c` refer to the warning and critical thresholds for the number of
workers _in use_.

There is also a Collectd plugin which reports the same statistics as the
official plugin and can be run in one of two ways. You can run it within
Collectd using the `exec` plugin by just passing the scoreboard:

```
LoadPlugin exec

<Plugin exec>
  Exec "username" "/usr/sbin/collectd_apache" "/var/run/httpd/httpd.scoreboard"
</Plugin>
```

Or you can run it outside of Collectd using the `unixsock` plugin by also
passing the socket:

        # /usr/sbin/collectd_apache /var/run/httpd/httpd.scoreboard /var/run/collectd-socket

The first form can work out the polling interval and hostname as these are
passed in by Collectd as environment variables however Collectd won't run
things as UID 0 which you might need to be able to read the scoreboard so you
can end up boxing around with `sudo -E`.

The second form cannot infer the polling interval so if the defaults of a 60
second polling interval and the FQDN of the host aren't desired then there are
`-i` and `-h` options to adjust these as necessary.
