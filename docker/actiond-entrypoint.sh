#!/bin/sh
# actiond's runtime image defaults to starting as root specifically so this script can chown a
# freshly bind-mounted /data/output before the real process ever runs -- Docker bind mounts take
# the *host* directory's ownership verbatim, so a host directory created by `docker compose up`
# (root-owned, as Docker itself creates it) would otherwise be unwritable by the non-root
# "actiond" user and every render.snapshot/session.save action would fail with a permission
# error. setpriv (util-linux, present in the base image already -- no extra package needed) then
# hands off to the real binary as uid/gid 10001 and never returns, so actiond itself never runs
# as root. If the container is started as a non-root user already (e.g. `docker run --user`),
# this skips straight to exec, since there is no root left to chown with.
set -e

if [ "$(id -u)" = "0" ]; then
    chown actiond:actiond /data/output 2>/dev/null || true
    exec setpriv --reuid=actiond --regid=actiond --clear-groups /app/bin/actiond "$@"
fi

exec /app/bin/actiond "$@"
