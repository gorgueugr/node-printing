#!/usr/bin/env bash
#
# End to end test for Linux: compiles the addon and runs it against a real CUPS
# daemon with two virtual queues backed by the file: backend, so the printed
# bytes can be verified on disk.
#
#   docker build -t node-printing-build test/docker
#   docker run --rm -v "$PWD":/work node-printing-build bash test/docker/e2e-cups.sh
#
# Inside the container the repository is expected at /work (override WORKDIR).

set -euo pipefail

WORKDIR="${WORKDIR:-/work}"
OUT_ACTIVE="${OUT_ACTIVE:-/tmp/nodeprinting-active.prn}"
OUT_HELD="${OUT_HELD:-/tmp/nodeprinting-held.prn}"
QUEUE_ACTIVE="${QUEUE_ACTIVE:-nodeprinting-active}"
QUEUE_HELD="${QUEUE_HELD:-nodeprinting-held}"
QUEUE_PPD="${QUEUE_PPD:-nodeprinting-ppd}"
OUT_PPD="${OUT_PPD:-/tmp/nodeprinting-ppd.prn}"

cd "$WORKDIR"

echo "### 1/7 compile the addon"
NODEDIR_ARGS=()
if [ -n "${npm_config_nodedir:-}" ]; then
    NODEDIR_ARGS=(--nodedir="${npm_config_nodedir}")
fi
rm -rf build
npx node-gyp rebuild "${NODEDIR_ARGS[@]}" >/tmp/node-printing-build.log 2>&1 || {
    echo "compilation failed:"
    tail -40 /tmp/node-printing-build.log
    exit 1
}
ls -l build/Release/nodeprinting.node

echo "### 2/7 start cupsd"
mkdir -p /run/cups
# A probe backend that writes the received document to the path in the device
# URI: Debian no longer ships the file: backend, and this makes the printed
# bytes verifiable.
install -m 755 -o root -g root "$WORKDIR/test/docker/backend-probe.sh" /usr/lib/cups/backend/npprobe
ls -l /usr/lib/cups/backend/npprobe
if [ -f /etc/cups/cups-files.conf ]; then
    # FileDevice is needed for file: URIs.
    sed -i 's/^#*FileDevice.*/FileDevice yes/' /etc/cups/cups-files.conf
    grep -q "^FileDevice yes" /etc/cups/cups-files.conf || echo "FileDevice yes" >>/etc/cups/cups-files.conf
    grep -E "^(FileDevice)" /etc/cups/cups-files.conf
fi
# CUPS hides job-name and job-originating-user-name from everybody but the job
# owner (JobPrivateValues default). The test verifies those fields, so privacy
# is turned off here; the README documents the stock behaviour.
if [ -f /etc/cups/cupsd.conf ]; then
    sed -i 's/^\( *\)JobPrivateAccess .*/\1JobPrivateAccess all/;
            s/^\( *\)JobPrivateValues .*/\1JobPrivateValues none/' /etc/cups/cupsd.conf
    grep -E "^ *Job(PrivateAccess|PrivateValues)" /etc/cups/cupsd.conf
fi
# `lpstat -r` always exits 0, so the output has to be checked.
cups_is_up() { lpstat -r 2>/dev/null | grep -q "is running"; }
# A bad configuration makes cupsd exit immediately without a message.
cupsd -t 2>&1 | sed 's/^/config: /' || true
if command -v cupsd >/dev/null 2>&1 && ! cups_is_up; then
    # Launched directly: the init script does not work in every container.
    { nohup cupsd >/tmp/cupsd.out 2>&1 & }
fi
for _ in $(seq 1 40); do
    if cups_is_up; then
        break
    fi
    sleep 0.5
done
if ! cups_is_up; then
    echo "CUPS did not start" >&2
    echo "--- /var/log/cups/error_log" >&2
    tail -20 /var/log/cups/error_log 2>/dev/null >&2
    echo "--- /tmp/cupsd.out" >&2
    tail -20 /tmp/cupsd.out 2>/dev/null >&2
    exit 1
fi
lpstat -r

echo "### 3/7 create the virtual queues"
rm -f "$OUT_ACTIVE" "$OUT_HELD"
lpadmin -p "$QUEUE_ACTIVE" -E -v "npprobe:$OUT_ACTIVE" -m raw \
    -D "node-printing e2e queue" -L "test lab" 2>&1
lpadmin -p "$QUEUE_HELD" -E -v "npprobe:$OUT_HELD" -m raw \
    -D "node-printing disabled queue" 2>&1
lpadmin -d "$QUEUE_ACTIVE" 2>&1
# Defaults reported by Get-Printer-Attributes (raw queues have no PPD).
lpadmin -p "$QUEUE_ACTIVE" -o print-color-mode=monochrome 2>&1
# A disabled queue keeps its jobs pending, which is what job control needs.
cupsdisable "$QUEUE_HELD" 2>&1
# A queue with a real PPD: its defaults and driver options are read from the
# page description file, which is how actual printers are configured.
rm -f "$OUT_PPD"
lpadmin -p "$QUEUE_PPD" -E -v "npprobe:$OUT_PPD" \
    -P "$WORKDIR/test/fixtures/generic-printer.ppd" \
    -D "node-printing PPD queue" 2>&1
lpadmin -p "$QUEUE_PPD" -o Duplex=DuplexNoTumble -o cupsPrintQuality=High -o PageSize=A4 2>&1
lpstat -p "$QUEUE_ACTIVE" -p "$QUEUE_HELD" -p "$QUEUE_PPD"

if [ "${SKIP_TESTS:-0}" = "1" ]; then
    echo "SKIP_TESTS=1: environment ready, not running the test suite"
    exit 0
fi

echo "### 4/7 smoke test (no printer required)"
node test/smoke.cjs

echo "### 5/7 spooler test against the real queues"
PRINTING_REQUIRE_SPOOLER=1 \
PRINTING_TEST_PRINTER="$QUEUE_ACTIVE" \
PRINTING_TEST_HELD_PRINTER="$QUEUE_HELD" \
PRINTING_TEST_PPD_PRINTER="$QUEUE_PPD" \
PRINTING_TEST_VERIFY_JOB_METADATA=1 \
PRINTING_TEST_BACKEND_FILE="$OUT_ACTIVE" \
node test/spooler.cjs

echo "### 6/7 what the queues wrote to disk"
ls -l "$OUT_ACTIVE" "$OUT_HELD" 2>&1 || true
echo "--- first bytes of the active queue output ---"
head -c 240 "$OUT_ACTIVE" | cat -v

echo "### 7/7 spooler state after the test"
lpstat -W all -o 2>&1 | tail -20

echo "E2E OK"
