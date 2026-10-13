#!/usr/bin/env node
'use strict';

/**
 * Shows the default configuration of every queue: paper, orientation, duplex,
 * colour, quality, tray and resolution.
 *
 *   node examples/printer-capabilities.cjs
 *   node examples/printer-capabilities.cjs "My Printer"
 */

const printing = require('..');

const requested = process.argv[2];
const printers = requested ? [printing.getPrinter(requested)] : printing.getPrinters();

for (const printer of printers) {
  console.log('=== ' + printer.name + ' (' + printer.state + ') ===');
  try {
    console.log(printing.getPrinterDevMode(printer.name));
  } catch (error) {
    console.log('  no default configuration: ' + error.message);
  }

  const options = printing.getPrinterDriverOptions(printer.name);
  const keywords = Object.keys(options);
  if (keywords.length > 0) {
    console.log('  driver options: ' + keywords.join(', '));
  }
}
