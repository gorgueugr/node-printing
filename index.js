import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const printer = require('./lib/printer.cjs');

const defaultPrinterName = printer.getDefaultPrinterName();

console.log(defaultPrinterName);

console.log(printer.getPrinters());

// console.log(printer.getSupportedJobCommands());

const printerName = "Microsoft Print to PDF";

console.log(printer.getPrinter(printerName));

const result = printer.printDirect('Hello world!', printerName, 'test.txt', 'RAW');
console.log(result);

// //console.log(printer.getSupportedPrintFormats());

// console.log(printer.getJob(defaultPrinterName, 1));
