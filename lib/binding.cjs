'use strict';

/**
 * Loads the compiled native addon.
 *
 * The addon is a plain .node file, so it is located by hand instead of relying
 * on node-gyp's default output directory. When it is missing the error message
 * explains what to install for the current platform.
 */

const path = require('path');
const fs = require('fs');

const candidates = [
  '../build/Release/nodeprinting.node',
  '../build/Debug/nodeprinting.node',
  '../prebuilds/' + process.platform + '-' + process.arch + '/nodeprinting.node',
];

let binding = null;
let lastError = null;

for (const candidate of candidates) {
  const file = path.join(__dirname, candidate);
  if (!fs.existsSync(file)) {
    continue;
  }
  try {
    binding = require(file);
    break;
  } catch (error) {
    lastError = error;
  }
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
