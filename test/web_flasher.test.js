const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const pagePath = path.join(__dirname, '..', 'docs', 'index.html');
const page = fs.readFileSync(pagePath, 'utf8');
const inlineScript = page.match(/<script>\s*([\s\S]*?)<\/script>/);

assert.ok(inlineScript, 'docs/index.html must contain an inline application script');

function makeElement() {
  const attributes = new Map();
  return {
    checked: false,
    children: [],
    classList: { add() {}, remove() {}, toggle() {} },
    innerHTML: '',
    rows: [],
    style: {},
    textContent: '',
    value: '',
    addEventListener() {},
    appendChild() {},
    getAttribute(name) { return attributes.get(name); },
    insertBefore() {},
    removeChild() {},
    setAttribute(name, value) { attributes.set(name, value); },
  };
}

function createHarness({ fetchImpl } = {}) {
  const elements = new Map();
  const blobs = new Map();
  let nextBlobId = 1;

  const element = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

  class TestURL extends URL {
    static createObjectURL(blob) {
      const url = `blob:test-${nextBlobId++}`;
      blobs.set(url, blob);
      return url;
    }

    static revokeObjectURL(url) {
      blobs.delete(url);
    }
  }

  const manifestUrl = 'https://example.test/flasher/manifest-atom-echo.json';
  const baseManifest = {
    name: 'Eye Spy -- M5Atom Echo',
    version: 'latest',
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: 'ESP32',
        parts: [
          { path: 'firmware/atom-echo/bootloader.bin', offset: 4096 },
          { path: 'firmware/atom-echo/firmware.bin', offset: 65536 },
        ],
      },
      {
        chipFamily: 'ESP32-S3',
        parts: [
          { path: 'firmware/atom-echo-s3/firmware.bin', offset: 0 },
        ],
      },
    ],
  };

  const sandbox = {
    Blob,
    DataView,
    TextDecoder,
    Uint8Array,
    URL: TestURL,
    clearTimeout,
    console,
    document: {
      createElement: () => makeElement(),
      getElementById: element,
      querySelectorAll: () => [],
    },
    fetch: fetchImpl || (async () => ({
        json: async () => structuredClone(baseManifest),
        ok: true,
        url: manifestUrl,
      })),
    navigator: {},
    setTimeout,
  };

  vm.createContext(sandbox);
  vm.runInContext(`${inlineScript[1]}
    globalThis.__eyeSpyTest = {
      applyManifest,
      setSelectedManifest(value) { selectedManifest = value; },
    };`, sandbox, { filename: pagePath });

  return { baseManifest, blobs, element, manifestUrl, sandbox };
}

async function generateConfiguredManifest() {
  const harness = createHarness();
  const { element, sandbox } = harness;

  element('cfg-custom').checked = true;
  element('cfg-eng-bodycam').checked = true;
  element('cfg-eng-trackers').checked = true;
  element('cfg-out-chirp').checked = true;
  element('cfg-minscore').value = '8';
  element('cfg-rssi').value = '-90';
  element('cfg-tracker').value = '60';

  sandbox.__eyeSpyTest.setSelectedManifest('manifest-atom-echo.json');
  await sandbox.__eyeSpyTest.applyManifest();

  const generatedUrl = element('install-btn').getAttribute('manifest');
  const generatedBlob = harness.blobs.get(generatedUrl);
  assert.ok(generatedBlob, 'install button must reference the generated manifest blob');

  return {
    ...harness,
    manifest: JSON.parse(await generatedBlob.text()),
  };
}

test('generated manifest resolves firmware paths and adds config to each build', async () => {
  const { manifest } = await generateConfiguredManifest();

  assert.equal(manifest.parts, undefined);
  assert.deepEqual(
    manifest.builds[0].parts.slice(0, 2).map((part) => part.path),
    [
      'https://example.test/flasher/firmware/atom-echo/bootloader.bin',
      'https://example.test/flasher/firmware/atom-echo/firmware.bin',
    ],
  );
  assert.equal(manifest.builds[0].parts[2].offset, 0x3ff000);
  assert.match(manifest.builds[0].parts[2].path, /^blob:test-/);
  assert.equal(
    manifest.builds[1].parts[0].path,
    'https://example.test/flasher/firmware/atom-echo-s3/firmware.bin',
  );
  assert.equal(manifest.builds[1].parts[1].offset, 0x3ff000);
  assert.equal(manifest.builds[1].parts[1].path, manifest.builds[0].parts[2].path);
});

test('generated config part contains the selected settings', async () => {
  const { blobs, manifest } = await generateConfiguredManifest();
  const configPart = manifest.builds[0].parts.find((part) => part.offset === 0x3ff000);
  assert.ok(configPart, 'selected settings must be present in the active build parts');

  const configBlob = blobs.get(configPart.path);
  assert.ok(configBlob, 'config part path must reference the generated settings blob');
  const bytes = new Uint8Array(await configBlob.arrayBuffer());
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

  assert.equal(bytes.length, 64);
  assert.equal(view.getUint32(0, true), 0x46435345);
  assert.equal(bytes[4], 1);
  assert.equal(view.getUint16(6, true), 2);       // chirp only
  assert.equal(view.getUint32(8, true), 0x0c81); // bodycam + three tracker bits
  assert.equal(bytes[12], 8);
  assert.equal(view.getInt8(13), -90);
  assert.equal(bytes[14], 60);
});

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((done, fail) => {
    resolve = done;
    reject = fail;
  });
  return { promise, reject, resolve };
}

function manifestBody(firmwarePath) {
  return {
    name: firmwarePath,
    builds: [{
      chipFamily: 'ESP32',
      parts: [{ path: firmwarePath, offset: 65536 }],
    }],
  };
}

function manifestResponse(url, firmwarePath, json = async () => manifestBody(firmwarePath)) {
  return {
    json,
    ok: true,
    status: 200,
    url,
  };
}

test('an older manifest response cannot replace the latest selection', async () => {
  const oldJson = deferred();
  const oldJsonStarted = deferred();
  const newFetch = deferred();
  const harness = createHarness({
    fetchImpl: (url) => url === 'old.json'
      ? manifestResponse('https://example.test/flasher/old.json', 'firmware/old.bin', () => {
          oldJsonStarted.resolve();
          return oldJson.promise;
        })
      : newFetch.promise,
  });
  harness.element('cfg-custom').checked = true;

  harness.sandbox.__eyeSpyTest.setSelectedManifest('old.json');
  const oldApply = harness.sandbox.__eyeSpyTest.applyManifest();
  await oldJsonStarted.promise;
  harness.sandbox.__eyeSpyTest.setSelectedManifest('new.json');
  const newApply = harness.sandbox.__eyeSpyTest.applyManifest();

  newFetch.resolve(manifestResponse(
    'https://example.test/flasher/new.json',
    'firmware/new.bin',
  ));
  await newApply;
  oldJson.resolve(manifestBody('firmware/old.bin'));
  await oldApply;

  const generatedUrl = harness.element('install-btn').getAttribute('manifest');
  const manifest = JSON.parse(await harness.blobs.get(generatedUrl).text());
  assert.equal(
    manifest.builds[0].parts[0].path,
    'https://example.test/flasher/firmware/new.bin',
  );
});

test('an older failed request cannot replace the latest configured manifest', async () => {
  const oldJson = deferred();
  const oldJsonStarted = deferred();
  const newFetch = deferred();
  const harness = createHarness({
    fetchImpl: (url) => url === 'old.json'
      ? manifestResponse('https://example.test/flasher/old.json', 'firmware/old.bin', () => {
          oldJsonStarted.resolve();
          return oldJson.promise;
        })
      : newFetch.promise,
  });
  harness.element('cfg-custom').checked = true;

  harness.sandbox.__eyeSpyTest.setSelectedManifest('old.json');
  const oldApply = harness.sandbox.__eyeSpyTest.applyManifest();
  await oldJsonStarted.promise;
  harness.sandbox.__eyeSpyTest.setSelectedManifest('new.json');
  const newApply = harness.sandbox.__eyeSpyTest.applyManifest();

  newFetch.resolve(manifestResponse(
    'https://example.test/flasher/new.json',
    'firmware/new.bin',
  ));
  await newApply;
  oldJson.reject(new Error('old manifest JSON failed'));
  await oldApply;

  assert.match(harness.element('install-btn').getAttribute('manifest'), /^blob:test-/);
});
