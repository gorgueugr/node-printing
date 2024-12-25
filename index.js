import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const printer = require('./lib/printer.cjs');

const defaultPrinterName = printer.getDefaultPrinterName();

console.log(defaultPrinterName);

console.log(printer.getPrinters());

// console.log(printer.getSupportedJobCommands());

console.log(printer.getPrinter(defaultPrinterName));

// //console.log(printer.getSupportedPrintFormats());

// console.log(printer.getJob(defaultPrinterName, 1));
