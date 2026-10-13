# node-printing

Native Node.js addon (N-API v8) to **manage printers and print jobs** from
JavaScript on **Windows, Linux and macOS**.

- **Windows**: Winspool / GDI (`EnumPrinters`, `GetPrinter`, `EnumJobs`,
  `SetJob`, `StartDocPrinter`, `DocumentProperties`).
- **Linux and macOS**: CUPS (`cupsGetDests`, `cupsCreateJob`, IPP `Get-Jobs`,
  `Cancel-Job`, `Hold-Job`, `Release-Job`, PPD defaults).

It is a complete rewrite of the original `node-printer` fork that lived in this
repository: the Windows backend was finished, the Linux/macOS backend was
written from scratch, the whole API was normalised so the same JavaScript code
works on every platform, and the package became usable as a real installable
module.

```js
const printing = require('printing');       // CommonJS
// import { getPrinters } from 'printing';  // ES modules

console.log(printing.getPrinters());

const jobId = printing.printDirect(Buffer.from('%!PS Hello'), 'office', 'hello.ps', 'POSTSCRIPT');
console.log('queued as', jobId);

printing.setJob('office', jobId, 'CANCEL');
```

---

## Table of contents

1. [Requirements and installation](#requirements-and-installation)
2. [Quick start](#quick-start)
3. [API reference](#api-reference)
4. [Data types](#data-types)
5. [Job commands and print formats per platform](#job-commands-and-print-formats-per-platform)
6. [Asynchronous printing](#asynchronous-printing)
7. [Errors](#errors)
8. [Testing](#testing)
9. [Architecture](#architecture)
10. [Migrating from the 1.x API](#migrating-from-the-1x-api)
11. [Limitations](#limitations)

---

## Requirements and installation

| Platform | Requirements |
| --- | --- |
| Linux | Node.js >= 18, python3, a C++17 compiler and the CUPS development headers: `sudo apt install libcups2-dev` (Debian/Ubuntu), `sudo dnf install cups-devel` (Fedora/RHEL), `apk add cups-dev` (Alpine). |
| macOS | Node.js >= 18 and the Xcode command line tools (`xcode-select --install`). CUPS and its headers ship with the system. |
| Windows | Node.js >= 18 and the Visual Studio Build Tools with the *Desktop development with C++* workload (`npm install --global windows-build-tools` is **not** needed on modern Node). |

The addon is compiled on install:

```bash
npm install printing
```

If the compile step is skipped (or you change the C++ sources), rebuild it:

```bash
npm run rebuild
```

`node-gyp` is the build tool; it is a devDependency of this package, so no global
installation is required.

## Quick start

```js
const printing = require('printing');

// 1. what is out there?
const printers = printing.getPrinters();
console.log(printers.map((p) => `${p.name} (${p.state})${p.isDefault ? ' [default]' : ''}`));

// 2. what is the default queue and how is it configured?
const defaultPrinter = printing.getDefaultPrinter();
if (defaultPrinter) {
  console.log(printing.getPrinterDevMode(defaultPrinter.name));
}

// 3. print
const jobId = printing.printDirect(
  Buffer.from('\x1b%-12345X@PJL ...', 'latin1'), // raw printer data
  defaultPrinter.name,
  'shipping-label',
  'RAW'
);

// 4. follow and control the job
console.log(printing.getJob(defaultPrinter.name, jobId));
for (const job of printing.getJobs(defaultPrinter.name)) {
  console.log(`#${job.id} ${job.state} ${job.user} ${job.name} ${job.size} bytes`);
}

printing.setJob(defaultPrinter.name, jobId, 'CANCEL');
```

ES modules work exactly the same:

```js
import { getPrinters, printFile } from 'printing';
```

## API reference

| Function | Description |
| --- | --- |
| `getPrinters()` | Every queue known to the spooler, as `PrinterInfo[]`. |
| `getDefaultPrinterName()` | Name of the default queue, `""` when none is configured. |
| `getDefaultPrinter()` | The default queue as a `PrinterInfo`, or `null`. |
| `getPrinter(printerName)` | One queue. `"queue/instance"` is accepted on CUPS. |
| `getPrinterDevMode(printerName)` | Default configuration: paper, orientation, duplex, colour, quality, tray, resolution. |
| `getPrinterDriverOptions(printerName)` | Driver options as `{ keyword: { choice: selected } }`. Empty object for raw queues. |
| `getJobs([printerName][, includeCompleted])` | Jobs of one queue, or of every queue when the name is omitted. `includeCompleted` (default `false`) adds finished and cancelled jobs. |
| `getJob(printerName, jobId)` | One job. |
| `setJob(printerName, jobId, command)` | Controls a job (cancel, pause/hold, resume/release, restart, delete...). Returns `true` when the spooler accepted the command. |
| `cancelJob(printerName, jobId)` | Shorthand for `setJob(printerName, jobId, 'CANCEL')`. |
| `getSupportedJobCommands()` | Commands accepted by `setJob` on this platform. |
| `getSupportedPrintFormats()` | Data types accepted by the print functions. |
| `printDirect(data, printerName[, docName][, type])` | Sends a string, `Buffer` or `TypedArray`. Returns the spooler job id. |
| `printDirectAsync(data, printerName[, docName][, type])` | Same, asynchronous: returns `Promise<number>`. |
| `printFile(filename, docName, printerName, type)` | Sends a file without loading it into memory. Returns the job id. |
| `printFileAsync(filename, docName, printerName, type)` | Same, asynchronous: returns `Promise<number>`. |

Notes:

- `type` defaults to `'RAW'` when omitted; see the format table below.
- `printDirect`/`printFile` return the **real spooler job id** (the Winspool job
  id or the CUPS job id) and only return once the spooler has accepted the
  document. Use the asynchronous variants to keep the event loop free while a
  large document is spooled.

## Data types

### `PrinterInfo`

| Field | Type | Meaning |
| --- | --- | --- |
| `name` | string | Queue name. |
| `instance` | string \| null | CUPS instance, `null` when there is none. |
| `server` | string | Print server; empty for local queues. |
| `shareName` | string | Share name (Windows). |
| `portName` | string | Port or device name. |
| `driverName` | string | Driver name (Windows) or *make and model* (CUPS). |
| `location` | string | Physical location. |
| `comment` | string | Description of the queue. |
| `uri` | string | Device URI (CUPS) or a port based identifier (Windows). |
| `state` | `'idle' \| 'processing' \| 'stopped' \| 'unknown'` | Canonical queue state. |
| `isDefault` | boolean | Is the default queue. |
| `isShared` | boolean | Shared with other machines. |
| `isLocal` | boolean | Local queue (not on a remote server). |
| `acceptsJobs` | boolean | Currently accepting new jobs. |
| `status` | number | Raw platform status code (`PRINTER_STATUS_*` / IPP `printer-state`). |
| `statusArray` | string[] | Canonical status names, e.g. `['idle']`, `['PRINTING']`, `['PAPER-OUT']`. |
| `attributes` | number | Raw attributes bitmask (Windows `PRINTER_ATTRIBUTE_*` / CUPS `printer-type`). |
| `attributeArray` | string[] | Canonical attribute names, e.g. `['COLOR', 'DUPLEX']`. |
| `averagePPM` | number | Pages per minute, `0` when unknown. |
| `cJobs` | number | Jobs queued for this printer. |
| `defaultPriority` | number | Default job priority. |
| `startTime`, `untilTime` | number | Availability window (Windows). |
| `options` | object | Raw platform options: CUPS destination options. Empty on Windows, where driver data is only reachable as multi-valued records and is exposed through `getPrinterDriverOptions()` instead. |

### `JobInfo`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | number | Spooler job id. |
| `name` | string | Document title. |
| `printerName` | string | Queue the job belongs to. |
| `user` | string | Submitting user. |
| `format` | string | `RAW`, `TEXT`, `PDF`... or the raw MIME type. |
| `state` | `'pending' \| 'processing' \| 'held' \| 'stopped' \| 'canceled' \| 'aborted' \| 'completed' \| 'unknown'` | Canonical state. |
| `status` | string | Human readable summary: `PRINTING`, `PAUSED`, `PRINTED`... |
| `statusArray` | string[] | Canonical reasons, e.g. `['PRINTING', 'PENDING']`. |
| `priority` | number | Job priority. |
| `size` | number | Document size in bytes. |
| `position` | number | 1 based position in the queue, `0` when unknown. |
| `totalPages`, `pagesPrinted` | number | Pages, `0` when unknown. |
| `submittedTime`, `completedTime`, `processingTime` | `Date \| null` | Timestamps, `null` when unknown. |

### `PrinterDevMode`

`deviceName`, `paperSize` (`'A4'`, `'LETTER'`...), `paperSizeCode`,
`mediaType`, `orientation` (`'PORTRAIT'`/`'LANDSCAPE'`), `duplex`
(`'SIMPLEX'`/`'VERTICAL'`/`'HORIZONTAL'`), `color`
(`'MONOCHROME'`/`'COLOR'`), `printQuality`
(`'DRAFT'`/`'LOW'`/`'MEDIUM'`/`'HIGH'`), `copies`, `collate`, `scale`,
`defaultSource`, `defaultSourceCode`, `resolutionX`, `resolutionY`.

Duplex follows the printing industry convention: `VERTICAL` is *long edge*
binding, `HORIZONTAL` is *short edge* binding.

## Job commands and print formats per platform

`getSupportedJobCommands()` is the authoritative list for the machine you are
running on. The same names mean the same thing everywhere, aliases included:

| Command | Windows (Winspool) | Linux/macOS (CUPS) |
| --- | --- | --- |
| `CANCEL` | `JOB_CONTROL_CANCEL` | IPP `Cancel-Job` |
| `DELETE` | `JOB_CONTROL_DELETE` | `Cancel-Job` with `purge-job` |
| `PAUSE`, `HOLD` | `JOB_CONTROL_PAUSE` / `JOB_CONTROL_RETAIN` | IPP `Hold-Job` |
| `RESUME`, `RELEASE` | `JOB_CONTROL_RESUME` / `JOB_CONTROL_RELEASE` | IPP `Release-Job` |
| `RESTART` | `JOB_CONTROL_RESTART` | IPP `Restart-Job` |
| `SENT-TO-PRINTER`, `LAST-PAGE-EJECTED` | `JOB_CONTROL_SENT_TO_PRINTER`, `JOB_CONTROL_LAST_PAGE_EJECTED` | not supported |

CUPS has no separate *pause* and *hold* operation (nor *resume* and *release*),
so `PAUSE`/`HOLD` map to `Hold-Job` and `RESUME`/`RELEASE` to `Release-Job`.
Windows-only commands are reported by an error naming the supported ones.

| `type` | Windows | Linux/macOS |
| --- | --- | --- |
| `RAW` | passed to `StartDocPrinter` | `application/vnd.cups-raw` |
| `TEXT` | `"TEXT"` | `text/plain` |
| `PDF` | `"PDF"` | `application/pdf` |
| `POSTSCRIPT` | `"POSTSCRIPT"` | `application/postscript` |
| `JPEG` | — | `image/jpeg` |
| `PNG` | — | `image/png` |
| `COMMAND` | — | `application/vnd.cups-command` |
| `AUTO` | — | `application/octet-stream` |
| a MIME type, e.g. `application/pdf` | passed as-is | passed as-is |

Use `RAW` to send bytes straight to the device (ZPL, PCL, ESC/POS, PJL...), which
is what most label and POS printers need.

## Asynchronous printing

`printDirect` and `printFile` block the event loop while the spooler accepts the
document. For big payloads use the asynchronous variants, which run on the libuv
thread pool and return a promise:

```js
const jobId = await printing.printDirectAsync(bigBuffer, 'office', 'big-report.ps', 'POSTSCRIPT');
```

## Errors

Every function throws an `Error` when the operation fails, carrying the message
of the spooler: for example `cannot create the print job on 'office': The printer
or class does not exist.` (CUPS) or `StartDocPrinter failed: code: 1801,
message: The printer name is invalid.` (Winspool). Argument problems throw a
`TypeError` instead. Nothing fails silently and nothing crashes the process.

## Testing

```bash
npm test              # smoke test + spooler test
npm run test:smoke    # loads the addon, checks the API and argument validation
npm run test:spooler  # exercises the real queues of this machine
```

- `test/smoke.cjs` needs **no** printer: it verifies that the addon loads, that
  both module systems expose the API and that bad arguments are rejected.
- `test/spooler.cjs` uses the queues of the machine, and can print for real:

  ```bash
  PRINTING_REQUIRE_SPOOLER=1 \
  PRINTING_TEST_PRINTER=my-queue \
  PRINTING_TEST_HELD_PRINTER=my-disabled-queue \
  PRINTING_TEST_BACKEND_FILE=/tmp/queue-output.prn \
  node test/spooler.cjs
  ```

  With `PRINTING_TEST_BACKEND_FILE` the test checks that the bytes written by the
  queue are **exactly** the bytes that were sent, through a `file:` backend.

### Full end to end test with a real CUPS (Linux)

`test/docker/e2e-cups.sh` compiles the addon, starts `cupsd`, creates three
virtual queues (a raw one, a disabled one for job control and one configured
with the test PPD), installs the small probe backend of
`test/docker/backend-probe.sh` — it writes the document it receives to the path
in the device URI — and runs the whole suite against them:

```bash
docker build -t node-printing-build test/docker
docker run --rm -v "$PWD":/work node-printing-build bash test/docker/e2e-cups.sh
```

GitHub Actions runs the smoke test on Linux, Windows and macOS, plus this end to
end test on Linux (see `.github/workflows/ci.yml`).

## Architecture

```
binding.gyp                       build description; one platform file per OS
src/PrinterManager.hpp            the platform interface (UTF-8, error strings)
src/node_printer.cpp              N-API layer, platform independent
src/win/WinPrinterManager.cpp     Windows backend  (Winspool, DEVMODE, Win32 errors)
src/posix/PosixPrinterManager.cpp Linux/macOS backend (CUPS, IPP, PPD)
lib/binding.cjs                   loads the compiled .node, explains build failures
lib/api.cjs                       public JS API
index.js / index.cjs / index.d.ts ESM, CommonJS and TypeScript entry points
```

`binding.gyp` compiles exactly one backend: `src/win/WinPrinterManager.cpp` on
Windows and `src/posix/PosixPrinterManager.cpp` elsewhere. Both implement the
same abstract `PrinterManager` class, so the N-API layer contains no platform
specific code. Strings cross the interface as UTF-8 and failures are reported as
error strings instead of exceptions.

## Migrating from the 1.x API

The 1.x API only worked on Windows and returned partially filled objects. The
2.0 API is compatible in spirit but not field by field:

- `getPrinter()` and `getJobs()` return richer objects: `state`,
  `attributeArray`, `options`, job `state`/`format`/`size`/timestamps.
- Linux and macOS actually work: listing, printing, jobs and job control.
- `setJob()` is exported on both platforms (it was commented out before), and
  `getSupportedJobCommands()` now reflects the real platform.
- `printDirect(data, printerName, docName, type)` and
  `printFile(filename, docName, printerName, type)` keep their argument order.
- New: `getJobs()`, `printFile()`, `getPrinterDriverOptions()`,
  `printDirectAsync()`, `printFileAsync()`, `cancelJob()`, `getDefaultPrinter()`.
- `getDefaultPrinterName()` returns `""` (not an exception) when no default queue
  is configured; `getDefaultPrinter()` returns `null`.

## Limitations

- CUPS *instances* (`queue/instance`) are resolved when reading a queue but the
  print path always targets the queue itself.
- CUPS job privacy: with a stock CUPS configuration `job-name` and
  `job-originating-user-name` are only returned to the owner of the job
  (`JobPrivateAccess default` / `JobPrivateValues default`), so `job.name` and
  `job.user` come back empty for other clients. Set `JobPrivateValues none` in
  `cupsd.conf` if every client has to see them. This is server behaviour, not a
  limitation of this package: CUPS hides those values from `lpstat` too.
- macOS is built and tested in CI but was developed against Linux CUPS; PPD
  defaults come from the same API on both.
- `position` and `totalPages` are Windows-only concepts in practice: CUPS does
  not always report them, so they can stay `0`.
- On Windows `processingTime` is never reported by the spooler (always `null`),
  and `completedTime` is derived from the job's `Time` field, which the spooler
  only fills for printed or complete jobs; `printerState`/`jobState` fall back to
  `unknown` for status bits outside the documented groups, while the raw names
  still appear in `statusArray`.
- The addon has no prebuilt binaries: it compiles on install, so a toolchain is
  required on the target machine.

## License

ISC. See [LICENSE](LICENSE).
