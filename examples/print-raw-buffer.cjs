#!/usr/bin/env node
'use strict';

/**
 * Sends raw bytes to a queue (ZPL, PCL, ESC/POS, PJL...) and follows the job.
 *
 *   node examples/print-raw-buffer.cjs "My Printer"
 */

const printing = require('..');

const printerName = process.argv[2] || printing.getDefaultPrinterName();
if (!printerName) {
  console.error('usage: node examples/print-raw-buffer.cjs "<printer name>"');
  process.exit(1);
}

// PJL header + a couple of ESC/POS lines, as a POS printer would expect.
const payload = Buffer.from(
  '\x1b%-12345X@PJL\r\n@PJL JOB NAME="node-printing example"\r\n' +
    '\x1b@Hello from node-printing\n\x1b\x64\x03\x1dV\x00',
  'latin1'
);

const jobId = printing.printDirect(payload, printerName, 'node-printing example', 'RAW');
console.log('queued job ' + jobId + ' (' + payload.length + ' bytes) on ' + printerName);
console.log(printing.getJob(printerName, jobId));
