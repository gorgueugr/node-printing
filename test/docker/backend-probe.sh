#!/bin/sh
#
# Minimal CUPS backend used by the end to end test.
#
# CUPS calls a backend as:  backend <job-id> <user> <title> <copies> <options> <job-file>
# and exports the device URI in DEVICE_URI. This backend appends the document
# it receives to the path taken from that URI, so the test can compare the bytes
# that reached the spooler with the bytes that were sent.
#
# It is installed as /usr/lib/cups/backend/npprobe (root owned, mode 755) by
# test/docker/e2e-cups.sh.

set -eu

uri="${DEVICE_URI:-}"
destination="${uri#*:}"

if [ -z "$destination" ] || [ "$destination" = "$uri" ]; then
    echo "ERROR: no destination in DEVICE_URI ($uri)" >&2
    exit 2
fi

job_file="${6:-}"
if [ -z "$job_file" ] || [ ! -f "$job_file" ]; then
    echo "ERROR: job file not readable ($job_file)" >&2
    exit 2
fi

# STATE: + is how a backend tells CUPS that everything is fine.
echo "STATE: +none"
cat "$job_file" >>"$destination" || exit 1
exit 0
