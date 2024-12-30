import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const printer = require('./lib/printer.cjs');

const defaultPrinterName = printer.getDefaultPrinterName();

console.log(defaultPrinterName);

console.log(printer.getPrinter(defaultPrinterName));

// console.log(printer.getPrinters());

// // console.log(printer.getSupportedJobCommands());

// // const printerName = "Microsoft Print to PDF";
// // const printerName = "WF-7830 Series(Red)";
// // // const printerName = "Microsoft XPS Document Writer";

// // console.log(printer.getPrinter(printerName));

// // const testPrint = fs.readFileSync('test.pcl');
// // const testPrint = "Hola Mundo".repeat(100);

// // console.log(testPrint);

// // const result = printer.printDirect(testPrint, printerName, 'test.PCL', 'RAW');
// // console.log(result);

// for (const print of printer.getPrinters()) {

//     console.log(printer.getPrinterDevMode(print.name));


// }


// console.log(printer.getSupportedPrintFormats());
// console.log(printer.getSupportedJobCommands());

// console.log(printer.getJob(defaultPrinterName, 1));
