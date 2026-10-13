/**
 * ES module entry point: `import { getPrinters } from 'printing'`.
 *
 * The native addon itself is CommonJS, so the wrapper is imported and its
 * functions are re-exported as named exports.
 */

import api from './lib/api.cjs';

export const getPrinters = api.getPrinters;
export const getDefaultPrinterName = api.getDefaultPrinterName;
export const getDefaultPrinter = api.getDefaultPrinter;
export const getPrinter = api.getPrinter;
export const getPrinterDevMode = api.getPrinterDevMode;
export const getPrinterDriverOptions = api.getPrinterDriverOptions;
export const getJobs = api.getJobs;
export const getJob = api.getJob;
export const setJob = api.setJob;
export const cancelJob = api.cancelJob;
export const getSupportedJobCommands = api.getSupportedJobCommands;
export const getSupportedPrintFormats = api.getSupportedPrintFormats;
export const printDirect = api.printDirect;
export const printDirectAsync = api.printDirectAsync;
export const printFile = api.printFile;
export const printFileAsync = api.printFileAsync;

export default api;
