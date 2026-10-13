'use strict';

/**
 * Public JavaScript API of node-printing.
 *
 * Every function is a thin wrapper over the native module: the native layer
 * throws an Error (with the message coming from the spooler) when something
 * fails, and returns real spooler data when it works. Two conveniences are
 * added here because they are awkward in native code: getDefaultPrinter() and
 * job control shorthands.
 */

const binding = require('./binding.cjs');

/**
 * All print queues of the system.
 * @returns {Array<object>} one object per queue (see README for the fields)
 */
function getPrinters() {
  return binding.getPrinters();
}

/**
 * Name of the default queue.
 * @returns {string} empty string when no default printer is configured
 */
function getDefaultPrinterName() {
  return binding.getDefaultPrinterName();
}

/**
 * One queue by name.
 * @param {string} printerName
 * @returns {object}
 */
function getPrinter(printerName) {
  return binding.getPrinter(printerName);
}

/**
 * The default queue as an object, or null when there is no default queue.
 * @returns {object|null}
 */
function getDefaultPrinter() {
  const name = binding.getDefaultPrinterName();
  if (!name) {
    return null;
  }
  return binding.getPrinter(name);
}

/**
 * Default configuration of a queue: paper size, orientation, duplex, colour,
 * quality, tray, resolution...
 * @param {string} printerName
 * @returns {object}
 */
function getPrinterDevMode(printerName) {
  return binding.getPrinterDevMode(printerName);
}

/**
 * Driver options of a queue as { option: { choice: selected } }.
 * Empty object for queues without driver options (for example raw queues).
 * @param {string} printerName
 * @returns {object}
 */
function getPrinterDriverOptions(printerName) {
  return binding.getPrinterDriverOptions(printerName);
}

/**
 * Jobs of one queue, or of every queue when printerName is omitted.
 * @param {string} [printerName]
 * @param {boolean} [includeCompleted=false]
 * @returns {Array<object>}
 */
function getJobs(printerName, includeCompleted) {
  if (typeof printerName === 'boolean' && includeCompleted === undefined) {
    return binding.getJobs(printerName);
  }
  return binding.getJobs(printerName, includeCompleted);
}

/**
 * One job by id.
 * @param {string} printerName
 * @param {number} jobId
 * @returns {object}
 */
function getJob(printerName, jobId) {
  return binding.getJob(printerName, jobId);
}

/**
 * Controls a job.
 * @param {string} printerName
 * @param {number} jobId
 * @param {string} command CANCEL, DELETE, PAUSE, RESUME, RESTART, HOLD, RELEASE...
 * @returns {boolean} true when the spooler accepted the command
 */
function setJob(printerName, jobId, command) {
  return binding.setJob(printerName, jobId, command);
}

/**
 * Cancels a job (shorthand for setJob(printerName, jobId, 'CANCEL')).
 * @returns {boolean}
 */
function cancelJob(printerName, jobId) {
  return binding.setJob(printerName, jobId, 'CANCEL');
}

/** Commands accepted by setJob on this platform. */
function getSupportedJobCommands() {
  return binding.getSupportedJobCommands();
}

/** Data types accepted by the print functions on this platform. */
function getSupportedPrintFormats() {
  return binding.getSupportedPrintFormats();
}

/**
 * Sends a string or a Buffer to a queue and waits for the spooler.
 * @param {string|Buffer|Uint8Array} data raw document bytes
 * @param {string} printerName
 * @param {string} [docName] document title shown in the queue
 * @param {string} type RAW, TEXT, PDF, POSTSCRIPT, JPEG, PNG, COMMAND, AUTO or a MIME type
 * @returns {number} spooler job id
 */
function printDirect(data, printerName, docName, type) {
  return binding.printDirect(data, printerName, docName, type);
}

/**
 * Same as printDirect but asynchronous, so the event loop keeps running while
 * a big document is spooled.
 * @returns {Promise<number>} resolves with the spooler job id
 */
function printDirectAsync(data, printerName, docName, type) {
  return binding.printDirectAsync(data, printerName, docName, type);
}

/**
 * Sends a file to a queue.
 * @param {string} filename path of the file to print
 * @param {string} docName document title shown in the queue
 * @param {string} printerName
 * @param {string} type data type (see printDirect)
 * @returns {number} spooler job id
 */
function printFile(filename, docName, printerName, type) {
  return binding.printFile(filename, docName, printerName, type);
}

/**
 * Same as printFile but asynchronous.
 * @returns {Promise<number>} resolves with the spooler job id
 */
function printFileAsync(filename, docName, printerName, type) {
  return binding.printFileAsync(filename, docName, printerName, type);
}

module.exports = {
  getPrinters,
  getDefaultPrinterName,
  getDefaultPrinter,
  getPrinter,
  getPrinterDevMode,
  getPrinterDriverOptions,
  getJobs,
  getJob,
  setJob,
  cancelJob,
  getSupportedJobCommands,
  getSupportedPrintFormats,
  printDirect,
  printDirectAsync,
  printFile,
  printFileAsync,
};
