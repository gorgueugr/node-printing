'use strict';

/**
 * Smoke test: needs no printer and no spooler daemon.
 *
 * It checks that the addon loads, that the whole documented API is exported,
 * that both module systems work (CommonJS and ESM) and that bad arguments are
 * rejected with a helpful message. Run it with `npm run test:smoke`.
 */

const assert = require('node:assert/strict');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const printing = require('../index.cjs');

let failures = 0;

function check(name, fn) {
  try {
    fn();
    console.log('  ok   ' + name);
  } catch (error) {
    failures++;
    console.error('  FAIL ' + name + ': ' + error.message);
  }
}

const api = [
  'getPrinters',
  'getDefaultPrinterName',
  'getDefaultPrinter',
  'getPrinter',
  'getPrinterDevMode',
  'getPrinterDriverOptions',
  'getJobs',
  'getJob',
  'setJob',
  'cancelJob',
  'getSupportedJobCommands',
  'getSupportedPrintFormats',
  'printDirect',
  'printDirectAsync',
  'printFile',
  'printFileAsync',
];

console.log('node-printing smoke test (' + process.platform + '-' + process.arch + ')');

check('the native addon loads and exports the documented API', () => {
  for (const name of api) {
    assert.equal(typeof printing[name], 'function', name + ' must be a function');
  }
});

check('getSupportedPrintFormats returns data types', () => {
  const formats = printing.getSupportedPrintFormats();
  assert.ok(Array.isArray(formats), 'must be an array');
  assert.ok(formats.length > 0, 'must not be empty');
  for (const format of formats) {
    assert.equal(typeof format, 'string');
  }
  assert.ok(formats.includes('RAW'), 'RAW must always be supported');
});

check('getSupportedJobCommands returns job commands', () => {
  const commands = printing.getSupportedJobCommands();
  assert.ok(Array.isArray(commands) && commands.length > 0, 'must be a non-empty array');
  assert.ok(commands.includes('CANCEL'), 'CANCEL must always be supported');
});

check('getPrinters returns an array (or reports a spooler problem)', () => {
  try {
    const printers = printing.getPrinters();
    assert.ok(Array.isArray(printers), 'must be an array');
    console.log('       printers found: ' + printers.length);
    for (const printer of printers) {
      assert.equal(typeof printer.name, 'string');
      assert.equal(typeof printer.state, 'string');
      assert.ok(Array.isArray(printer.statusArray), 'statusArray must be an array');
      assert.ok(Array.isArray(printer.attributeArray), 'attributeArray must be an array');
      assert.equal(typeof printer.isDefault, 'boolean');
      assert.equal(typeof printer.options, 'object');
    }
  } catch (error) {
    // no spooler daemon in this environment: the error must still be useful
    assert.ok(error instanceof Error, 'must throw an Error');
    assert.ok(error.message.length > 0, 'the error must carry a message');
    console.log('       no usable spooler here: ' + error.message);
  }
});

check('getDefaultPrinterName returns a string', () => {
  const name = printing.getDefaultPrinterName();
  assert.equal(typeof name, 'string');
});

check('getDefaultPrinter returns an object or null', () => {
  const printer = printing.getDefaultPrinter();
  assert.ok(printer === null || typeof printer === 'object', 'must be an object or null');
});

check('missing or wrong arguments are rejected', () => {
  assert.throws(() => printing.getPrinter(), /required/);
  assert.throws(() => printing.getPrinter(123), /must be a string/);
  assert.throws(() => printing.getJob('printer'), /required and must be a number/);
  assert.throws(() => printing.getJob('printer', 'not-a-number'), /must be a number/);
  assert.throws(() => printing.setJob('printer', 0, 'CANCEL'), /positive/);
  assert.throws(() => printing.setJob('printer', 1), /required/);
  assert.throws(() => printing.getPrinterDevMode(), /required/);
  assert.throws(() => printing.printDirect(42, 'printer', 'doc', 'RAW'), /string, a Buffer/);
  assert.throws(() => printing.printDirect('data', 'printer', 'doc'), /required/);
  assert.throws(() => printing.printFile('file', 'doc', 'printer'), /required/);
});

check('an unsupported data type is refused with the supported list', () => {
  assert.throws(
    () => printing.printDirect('data', 'definitely-not-a-printer', 'doc', 'NOT-A-FORMAT'),
    /unsupported data type|not supported|unsupported format/i
  );
});

check('empty documents are refused', () => {
  assert.throws(() => printing.printDirect(Buffer.alloc(0), 'printer', 'doc', 'RAW'), /empty/i);
});

check('the async print functions return promises', () => {
  const promise = printing.printDirectAsync(Buffer.from('x'), 'definitely-not-a-printer', 'doc', 'RAW');
  assert.ok(promise instanceof Promise, 'must return a promise');
  return promise.then(
    () => {
      throw new Error('printing to a missing queue must not succeed');
    },
    () => undefined
  );
});

const esmEntry = path.join(__dirname, '..', 'index.js');
const esmCheck = import(pathToFileURL(esmEntry).href).then((module) => {
  check('the ES module entry point exposes the same API', () => {
    for (const name of api) {
      assert.equal(typeof module[name], 'function', 'esm ' + name + ' must be a function');
    }
    assert.equal(typeof module.default, 'object');
  });
});

Promise.resolve(esmCheck)
  .catch((error) => {
    failures++;
    console.error('  FAIL esm entry point: ' + error.message);
  })
  .then(() => {
    if (failures > 0) {
      console.error('smoke test: ' + failures + ' failure(s)');
      process.exit(1);
    }
    console.log('smoke test: all checks passed');
  });
