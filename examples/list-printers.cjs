#!/usr/bin/env node
'use strict';

/**
 * Lists every queue of the machine and the default one.
 *
 *   node examples/list-printers.cjs
 */

const printing = require('..');

let printers;
let defaultPrinterName = '';
try {
  defaultPrinterName = printing.getDefaultPrinterName();
  printers = printing.getPrinters();
} catch (error) {
  // The message says what is missing: no CUPS server, wrong platform build...
  console.error(error.message);
  process.exit(1);
}

console.log('default printer: ' + (defaultPrinterName || '(none)'));
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
