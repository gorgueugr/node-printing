'use strict';

/**
 * Loads the compiled native addon.
 *
 * The addon is a plain .node file, so it is located by hand instead of relying
 * on node-gyp's default output directory. Two things are checked before loading
 * it, because both are easy to get wrong:
 *
 *  - the file has to exist for this platform, and `npm rebuild` has to be run
 *    where the code will run: a build made inside WSL cannot be used by Windows
 *    Node, and the other way round,
 *  - the executable format has to match the platform, so a binary left behind by
 *    another operating system is reported clearly instead of with a cryptic
 *    loader error.
 */

const path = require('path');
const fs = require('fs');

const candidates = [
  '../build/Release/nodeprinting.node',
  '../build/Debug/nodeprinting.node',
  '../prebuilds/' + process.platform + '-' + process.arch + '/nodeprinting.node',
];

/**
 * Detects which operating system a compiled addon was built for, from the first
 * bytes of the file: PE for Windows, ELF for Linux, Mach-O for macOS.
 * @param {string} file
 * @returns {string|null} a process.platform value, or null when unknown
 */
function binaryPlatform(file) {
  let handle = null;
  try {
    handle = fs.openSync(file, 'r');
    const header = Buffer.alloc(4);
    if (fs.readSync(handle, header, 0, 4, 0) !== 4) {
      return null;
    }
    if (header[0] === 0x4d && header[1] === 0x5a) {
      return 'win32'; // "MZ"
    }
    if (header[0] === 0x7f && header[1] === 0x45 && header[2] === 0x4c && header[3] === 0x46) {
      return 'linux'; // 0x7f "ELF"
    }
    const magic = header.readUInt32BE(0);
    const machO = [
      0xfeedface, // 32 bit
      0xfeedfacf, // 64 bit
      0xcafebabe, // universal
      0xcefaedfe, // byte swapped, 32 bit
      0xcffaedfe, // byte swapped, 64 bit
    ];
    if (machO.includes(magic)) {
      return 'darwin';
    }
    return null;
  } catch (error) {
    return null;
  } finally {
    if (handle !== null) {
      fs.closeSync(handle);
    }
  }
}

let binding = null;
let wrongPlatform = null;
let lastError = null;

for (const candidate of candidates) {
  const file = path.join(__dirname, candidate);
  if (!fs.existsSync(file)) {
    continue;
  }

  const builtFor = binaryPlatform(file);
  if (builtFor !== null && builtFor !== process.platform) {
    wrongPlatform = { file, builtFor };
    continue;
  }

  try {
    binding = require(file);
    break;
  } catch (error) {
    lastError = error;
  }
}

if (binding === null && wrongPlatform !== null) {
  throw new Error(
    'node-printing: the native addon at ' +
      wrongPlatform.file +
      ' was built for ' +
      wrongPlatform.builtFor +
      ', but this process is ' +
      process.platform +
      '-' +
      process.arch +
      '.\n' +
      'Native addons are not portable, so rebuild it where the code will run:\n' +
      '  npm run rebuild\n' +
      'Run that from the platform that will execute the code (on Windows use\n' +
      'PowerShell or cmd, not WSL; inside WSL use a Linux shell: they are two\n' +
      'different builds and they do not see the same printers).\n' +
      'Deleting the build/ directory also forces a clean rebuild.'
  );
}

if (binding === null) {
  const root = path.resolve(__dirname, '..');
  let prerequisites;
  if (process.platform === 'win32') {
    prerequisites =
      'Install Visual Studio Build Tools with the "Desktop development with C++" workload.';
  } else if (process.platform === 'darwin') {
    prerequisites = 'Install the Xcode command line tools: xcode-select --install';
  } else {
    prerequisites =
      'Install the CUPS development headers (Debian/Ubuntu: sudo apt install libcups2-dev, ' +
      'Fedora/RHEL: sudo dnf install cups-devel, Alpine: apk add cups-dev).';
  }

  throw new Error(
    'node-printing: native addon not built for ' +
      process.platform +
      '-' +
      process.arch +
      '.\n' +
      'Build it with `npm install` (or `npm rebuild`) inside ' +
      root +
      '.\n' +
      'Requirements: Node.js >= 18, python3, a C++17 compiler. ' +
      prerequisites +
      (lastError ? '\nLast load error: ' + lastError.message : '')
  );
}

module.exports = binding;
