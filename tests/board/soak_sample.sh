#!/bin/sh
# Runs ON THE BOARD: soak sampler of icwmpd (gate G9).  Every $1 s (default
# 600) one CSV row in /tmp/g9.csv: pid, VmRSS, fd, threads of icwmp_tr098d,
# the session counters of "ubus call tr069 status", MemAvailable and the
# number of agent starts in logread.  Stops after $2 rows (default 150 =
# 25 h at 600 s).  A healthy agent keeps pid and starts, RSS/fd/threads flat,
# failure 0.  The board has no setsid/nohup; start it detached with:
#   start-stop-daemon -S -b -m -p /tmp/g9.pid -x /bin/sh -- /tmp/soak_sample.sh 600 150
# stop: start-stop-daemon -K -p /tmp/g9.pid
I=${1:-600}; N=${2:-150}
echo "time,pid,vmrss_kb,fds,threads,success,failure,memavail_kb,starts" > /tmp/g9.csv
i=0
while [ $i -lt $N ]; do
	p=$(cat /var/run/icwmpd.pid 2>/dev/null)
	[ -n "$p" ] || p=$(ps w | grep '[i]cwmp_tr098d' | awk '{print $1}' | head -1)
	rss=$(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null)
	thr=$(awk '/Threads/{print $2}' /proc/$p/status 2>/dev/null)
	fds=$(ls /proc/$p/fd 2>/dev/null | wc -l)
	st=$(ubus -t 5 call tr069 status 2>/dev/null)
	ok=$(echo "$st" | sed -n 's/.*"success_sessions": \([0-9]*\).*/\1/p')
	ko=$(echo "$st" | sed -n 's/.*"failure_sessions": \([0-9]*\).*/\1/p')
	ma=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
	starts=$(logread | grep -c '==== start')
	echo "$(date +%FT%T),$p,$rss,$fds,$thr,$ok,$ko,$ma,$starts" >> /tmp/g9.csv
	i=$((i + 1))
	sleep "$I"
done
