import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';

// Independent controller decoder; never print the private onboarding payload.
const [manifestPath, modulesPath] = process.argv.slice(2);
assert(manifestPath && modulesPath, 'Usage: node check_pairing.mjs manifest.json node_modules');
const { QrPairingCodeCodec, ManualPairingCodeCodec } = await import(pathToFileURL(resolve(modulesPath, '@matter/types/dist/esm/schema/PairingCodeSchema.js')));
const m = JSON.parse(await readFile(manifestPath, 'utf8'));
const [qr] = QrPairingCodeCodec.decode(m.qr);
assert(qr.passcode === m.passcode, 'QR passcode mismatch');
assert(qr.discriminator === m.discriminator, 'QR discriminator mismatch');
assert(qr.vendorId === m.vendor, 'QR vendor mismatch');
assert(qr.productId === m.product, 'QR product mismatch');
assert(qr.discoveryCapabilities === 2, 'QR must advertise BLE only');
const manual = ManualPairingCodeCodec.decode(m.manual_code);
assert(manual.passcode === m.passcode, 'Manual passcode mismatch');
assert(manual.shortDiscriminator === m.discriminator >> 8, 'Manual discriminator mismatch');
console.log('PASS independent Matter.js QR/manual decoder: PIN, discriminator and product identity agree');
