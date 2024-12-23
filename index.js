import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const addon = require('./lib/addon.cjs');

console.log(addon.double(5)); // Debería imprimir 10
