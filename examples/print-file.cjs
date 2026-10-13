#!/usr/bin/env node
'use strict';

/**
 * Prints a PDF (or PostScript, or text) file.
 *
 *   node examples/print-file.cjs report.pdf "My Printer"
 */

const fs = require('node:fs');
const path = require('node:path');
const printing = require('..');

const filename = process.argv[2];
const printerName = process.argv[3] || printing.getDefaultPrinterName();

if (!filename || !printerName) {
  console.error('usage: node examples/print-file.cjs <file> ["<printer name>"]');
  process.exit(1);
}
if (!fs.existsSync(filename)) {
  console.error('no such file: ' + filename);
  process.exit(1);
}

const extension = path.extname(filename).toLowerCase();
const types = { '.pdf': 'PDF', '.ps': 'POSTSCRIPT', '.txt': 'TEXT', '.jpg': 'JPEG', '.jpeg': 'JPEG' };
const type = types[extension] || 'RAW';

const jobId = printing.printFile(filename, path.basename(filename), printerName, type);
console.log('queued job ' + jobId + ' (' + type + ') on ' + printerName);
