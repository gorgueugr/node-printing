#!/usr/bin/env node
'use strict';

/**
 * Monitors the queue: lists active jobs and, with --cancel <id>, cancels one.
 *
 *   node examples/jobs.cjs
 *   node examples/jobs.cjs --cancel 42
 *   node examples/jobs.cjs --cancel-all "My Printer"
 */

const printing = require('..');

const args = process.argv.slice(2);
const cancelIndex = args.indexOf('--cancel');
const cancelAllIndex = args.indexOf('--cancel-all');

function describe(job) {
  return [
    '#' + String(job.id).padEnd(6),
    job.state.padEnd(11),
    (job.user || '-').padEnd(12),
    String(job.size).padStart(9) + ' B',
    (job.printerName || '-').padEnd(20),
    job.name || '-',
  ].join(' ');
}

if (cancelIndex !== -1) {
  const jobId = Number(args[cancelIndex + 1]);
  const printerName = args[cancelIndex + 2] || printing.getDefaultPrinterName();
  const done = printing.setJob(printerName, jobId, 'CANCEL');
  console.log((done ? 'cancelled' : 'could not cancel') + ' job ' + jobId + ' on ' + printerName);
  process.exit(0);
}

if (cancelAllIndex !== -1) {
  const printerName = args[cancelAllIndex + 1] || printing.getDefaultPrinterName();
  for (const job of printing.getJobs(printerName)) {
    console.log((printing.cancelJob(printerName, job.id) ? 'cancelled ' : 'failed    ') + describe(job));
  }
  process.exit(0);
}

const jobs = printing.getJobs(undefined, true);
console.log(jobs.length + ' job(s) known to the spooler:');
for (const job of jobs) {
  console.log(describe(job));
}

if (jobs.length === 0) {
  console.log('(nothing queued: run examples/print-raw-buffer.cjs first)');
}
