#!/usr/bin/env node
'use strict';

/**
 * Lists every queue of the machine and the default one.
 *
 *   node examples/list-printers.cjs
 */

const printing = require('..');

const printers = printing.getPrinters();

console.log('default printer: ' + (printing.getDefaultPrinterName() || '(none)'));
console.log(printers.length + ' queue(s):');

for (const printer of printers) {
  console.log(
    [
      printer.isDefault ? '*' : ' ',
      printer.name.padEnd(32),
      printer.state.padEnd(11),
      printer.statusArray.join(',') || '-',
      printer.driverName || printer.portName || '-',
    ].join(' ')
  );
}

console.log('\nsupported data types : ' + printing.getSupportedPrintFormats().join(', '));
console.log('supported commands   : ' + printing.getSupportedJobCommands().join(', '));
