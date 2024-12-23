import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const printer = require('./lib/printer.cjs');

console.log(printer.getDefaultPrinterName()); // Debería imprimir 10
