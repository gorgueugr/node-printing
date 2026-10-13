'use strict';

/**
 * Spooler test: exercises the real printers of the machine.
 *
 * Without a working spooler the test reports SKIPPED and exits 0, so it can run
 * on any machine. Set these environment variables to make it strict and to test
 * the printing and job control paths end to end:
 *
 *   PRINTING_REQUIRE_SPOOLER=1     fail (instead of skip) when there is no spooler
 *   PRINTING_TEST_PRINTER=name     queue used for the print tests
 *   PRINTING_TEST_HELD_PRINTER=name  disabled queue, so jobs stay pending
 *   PRINTING_TEST_BACKEND_FILE=path  file written by the queue, to verify bytes
 *   PRINTING_TEST_PPD_PRINTER=name   queue with a PPD, used for the default
 *                                    configuration and driver option checks
 *   PRINTING_TEST_VERIFY_JOB_METADATA=1  also assert the job title and user.
 *                                    CUPS hides them from everybody but the job
 *                                    owner unless JobPrivateValues is set to
 *                                    none, so this is off by default.
 */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const printing = require('../index.cjs');

const requireSpooler = process.env.PRINTING_REQUIRE_SPOOLER === '1';
const testPrinter = process.env.PRINTING_TEST_PRINTER || '';
const heldPrinter = process.env.PRINTING_TEST_HELD_PRINTER || '';
const backendFile = process.env.PRINTING_TEST_BACKEND_FILE || '';
const ppdPrinter = process.env.PRINTING_TEST_PPD_PRINTER || '';
const verifyJobMetadata = process.env.PRINTING_TEST_VERIFY_JOB_METADATA === '1';

let checks = 0;

function ok(name, value) {
  checks++;
  assert.ok(value, name);
  console.log('  ok   ' + name);
}

function equal(name, actual, expected) {
  checks++;
  assert.equal(actual, expected, name + ': expected ' + expected + ', got ' + actual);
  console.log('  ok   ' + name);
}

function skip(message) {
  console.log('SKIPPED: ' + message);
  process.exit(0);
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function waitFor(description, predicate, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const result = await predicate();
    if (result) {
      checks++;
      console.log('  ok   ' + description);
      return result;
    }
    if (Date.now() > deadline) {
      throw new Error('timeout waiting for: ' + description);
    }
    await sleep(250);
  }
}

function readBackendFile() {
  try {
    return fs.readFileSync(backendFile);
  } catch (error) {
    return Buffer.alloc(0);
  }
}

/** Number of times `needle` appears in `haystack`. */
function countOccurrences(haystack, needle) {
  let count = 0;
  let offset = haystack.indexOf(needle);
  while (offset !== -1) {
    count++;
    offset = haystack.indexOf(needle, offset + needle.length);
  }
  return count;
}

async function main() {
  console.log('node-printing spooler test (' + process.platform + '-' + process.arch + ')');

  /* ---------------------------------------------------------------- */
  /* listing                                                          */
  /* ---------------------------------------------------------------- */

  let printers;
  try {
    printers = printing.getPrinters();
  } catch (error) {
    if (requireSpooler) {
      throw error;
    }
    skip('no usable spooler: ' + error.message);
  }

  ok('getPrinters() returns an array', Array.isArray(printers));
  console.log('       ' + printers.length + ' queue(s) found');

  for (const printer of printers) {
    assert.equal(typeof printer.name, 'string');
    assert.ok(printer.name.length > 0, 'every queue must have a name');
    assert.ok(
      ['idle', 'processing', 'stopped', 'unknown'].includes(printer.state),
      'unexpected state: ' + printer.state
    );
    assert.ok(Array.isArray(printer.statusArray));
    assert.equal(typeof printer.isDefault, 'boolean');
    assert.equal(typeof printer.acceptsJobs, 'boolean');
  }
  if (printers.length > 0) {
    checks++;
    console.log('  ok   every queue has a valid shape');
  }

  const defaultName = printing.getDefaultPrinterName();
  ok('getDefaultPrinterName() returns a string', typeof defaultName === 'string');
  if (defaultName) {
    const first = printers[0];
    const printed = printing.getPrinter(defaultName);
    equal('getPrinter(default) returns that queue', printed.name, defaultName);
    assert.ok(first !== undefined);
  }

  assert.throws(
    () => printing.getPrinter('node-printing-does-not-exist'),
    /not[- ]found|no such|does not exist/i,
    'an unknown queue must be reported'
  );
  checks++;
  console.log('  ok   unknown queues are reported as errors');

  /* ---------------------------------------------------------------- */
  /* one queue in detail                                              */
  /* ---------------------------------------------------------------- */

  if (!testPrinter) {
    console.log('no PRINTING_TEST_PRINTER set: listing checks only');
    console.log('spooler test: ' + checks + ' checks passed');
    return;
  }

  const printer = printing.getPrinter(testPrinter);
  equal('getPrinter(' + testPrinter + ') name', printer.name, testPrinter);
  ok('getPrinter() reports a device uri', typeof printer.uri === 'string');
  ok('getPrinter() reports options', typeof printer.options === 'object');
  equal('the test queue is the default queue', printer.isDefault, true);

  try {
    // A raw queue has no PPD: it can only report what IPP exposes.
    const devMode = printing.getPrinterDevMode(testPrinter);
    ok('getPrinterDevMode() returns an object', typeof devMode === 'object');
    console.log('       ' + JSON.stringify(devMode));
    ok('the colour mode default of the queue is reported', devMode.color === 'MONOCHROME');
  } catch (error) {
    if (requireSpooler) {
      throw error;
    }
    console.log('       getPrinterDevMode unavailable: ' + error.message);
  }

  const driverOptions = printing.getPrinterDriverOptions(testPrinter);
  ok('getPrinterDriverOptions() returns an object', typeof driverOptions === 'object');

  if (ppdPrinter) {
    // This queue is configured with a PPD, so every default comes from it:
    // PageSize A4, Duplex long edge and cupsPrintQuality High are set by the
    // end to end script through lpadmin.
    const ppdDevMode = printing.getPrinterDevMode(ppdPrinter);
    console.log('       ppd devMode ' + JSON.stringify(ppdDevMode));
    equal('the PPD paper size is reported', ppdDevMode.paperSize, 'A4');
    equal('the PPD duplex default is reported', ppdDevMode.duplex, 'VERTICAL');
    equal('the PPD colour default is reported', ppdDevMode.color, 'MONOCHROME');
    equal('the PPD quality default is reported', ppdDevMode.printQuality, 'HIGH');
    equal('the PPD tray default is reported', ppdDevMode.defaultSource, 'Auto');
    equal('the PPD media type default is reported', ppdDevMode.mediaType, 'Plain');
    equal('the PPD resolution is reported', ppdDevMode.resolutionX, 600);
    equal('the PPD device name is the queue name', ppdDevMode.deviceName, ppdPrinter);

    const ppdOptions = printing.getPrinterDriverOptions(ppdPrinter);
    ok('the PPD driver options are listed', Object.keys(ppdOptions).length > 0);
    ok(
      'the marked PageSize choice is the default one',
      ppdOptions.PageSize && ppdOptions.PageSize.A4 === true && ppdOptions.PageSize.Letter === false
    );
    ok(
      'the marked Duplex choice is the configured one',
      ppdOptions.Duplex &&
        ppdOptions.Duplex.DuplexNoTumble === true &&
        ppdOptions.Duplex.None === false
    );
    ok(
      'the marked quality choice is the configured one',
      ppdOptions.cupsPrintQuality &&
        ppdOptions.cupsPrintQuality.High === true &&
        ppdOptions.cupsPrintQuality.Normal === false
    );
  } else {
    console.log('no PRINTING_TEST_PPD_PRINTER set: PPD checks skipped');
  }

  /* ---------------------------------------------------------------- */
  /* printing                                                         */
  /* ---------------------------------------------------------------- */

  const payload = Buffer.from('node-printing e2e ' + Date.now() + '\n', 'utf8');
  const jobId = printing.printDirect(payload, testPrinter, 'node-printing raw probe', 'RAW');
  ok('printDirect() returns a job id', Number.isInteger(jobId) && jobId > 0);
  console.log('       job id ' + jobId);

  const job = await waitFor('the job shows up in getJobs()', () => {
    const jobs = printing.getJobs(testPrinter, true);
    return jobs.find((entry) => entry.id === jobId) || false;
  });
  if (verifyJobMetadata) {
    equal('the job title is reported', job.name, 'node-printing raw probe');
    ok('the submitting user is reported', job.user.length > 0);
  } else if (job.name === '') {
    console.log(
      '  note CUPS hides job-name/job-originating-user-name from non owners ' +
        '(JobPrivateValues), so the title is empty here'
    );
  } else {
    equal('the job title is reported', job.name, 'node-printing raw probe');
  }
  equal('the job belongs to the queue', job.printerName, testPrinter);
  ok('the job format is reported', job.format === 'RAW' || job.format.length > 0);
  ok('the job size matches the payload', job.size >= payload.length);
  ok('the job has a spooler state', typeof job.state === 'string');

  const fetched = printing.getJob(testPrinter, jobId);
  equal('getJob() returns the same job', fetched.id, jobId);

  // Listing every queue at once is a documented call: getJobs() without a name.
  const allJobs = printing.getJobs(true);
  ok(
    'getJobs() without a queue lists the jobs of every queue',
    Array.isArray(allJobs) && allJobs.some((entry) => entry.id === jobId)
  );

  assert.throws(
    () => printing.getJob(testPrinter, 987654321),
    /not[- ]found|no such|unknown|does not exist/i,
    'an unknown job id must be reported'
  );
  checks++;
  console.log('  ok   unknown job ids are reported as errors');

  if (backendFile) {
    const written = await waitFor('the queue wrote the document to ' + backendFile, () => {
      const content = readBackendFile();
      return content.indexOf(payload) !== -1 ? content : false;
    });
    ok('the printed bytes contain the bytes that were sent', written.indexOf(payload) !== -1);
    equal('the document was written exactly once', countOccurrences(written, payload), 1);
  }

  const fixture = path.join(__dirname, 'fixtures', 'hello.txt');
  const fileJobId = printing.printFile(fixture, 'node-printing file probe', testPrinter, 'TEXT');
  ok('printFile() returns a job id', Number.isInteger(fileJobId) && fileJobId > 0);

  if (backendFile) {
    const fixtureBytes = fs.readFileSync(fixture);
    const written = await waitFor('the file document reached the queue', () => {
      const content = readBackendFile();
      return content.indexOf(fixtureBytes) !== -1 ? content : false;
    });
    ok('the file was printed byte for byte', written.indexOf(fixtureBytes) !== -1);
  }

  const asyncJobId = await printing.printDirectAsync(
    Buffer.from('async probe\n'),
    testPrinter,
    'node-printing async probe',
    'RAW'
  );
  ok('printDirectAsync() resolves with a job id', Number.isInteger(asyncJobId) && asyncJobId > 0);

  assert.throws(
    () => printing.printFile('/definitely/not/a/file', 'doc', testPrinter, 'TEXT'),
    /cannot read|no such|not[- ]found|does not exist|failed/i,
    'a missing file must be reported'
  );
  checks++;
  console.log('  ok   missing files are reported as errors');

  /* ---------------------------------------------------------------- */
  /* job control                                                      */
  /* ---------------------------------------------------------------- */

  if (heldPrinter) {
    const heldPayload = Buffer.from('held probe\n');
    const heldJobId = printing.printDirect(heldPayload, heldPrinter, 'held probe', 'RAW');
    ok('a job can be queued on a disabled queue', heldJobId > 0);

    const held = printing.getJobs(heldPrinter, true).find((entry) => entry.id === heldJobId);
    ok('the held job is visible', held !== undefined);
    console.log('       held job state: ' + (held && held.state));

    ok('setJob(HOLD) is accepted', printing.setJob(heldPrinter, heldJobId, 'HOLD') === true);
    ok('setJob(RELEASE) is accepted', printing.setJob(heldPrinter, heldJobId, 'RELEASE') === true);
    ok('setJob(CANCEL) is accepted', printing.cancelJob(heldPrinter, heldJobId) === true);

    await waitFor('the cancelled job is no longer active', () => {
      const active = printing.getJobs(heldPrinter, false);
      return active.find((entry) => entry.id === heldJobId) === undefined;
    });

    assert.throws(
      () => printing.setJob(heldPrinter, heldJobId, 'NOT-A-COMMAND'),
      /wrong job command|not supported/i,
      'an unknown job command must be reported'
    );
    checks++;
    console.log('  ok   unknown job commands are reported as errors');
  } else {
    console.log('no PRINTING_TEST_HELD_PRINTER set: job control checks skipped');
  }

  console.log('spooler test: ' + checks + ' checks passed');
}

main().catch((error) => {
  console.error('FAILED: ' + (error && error.stack ? error.stack : error));
  process.exit(1);
});
