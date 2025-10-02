// cancel_live.js: Play far-end WAV, record mic, cancel echo with AEC3, save to processed.wav
// Usage: node cancel_live.js <render.wav> [--no-linear] [--no-nonlinear]

const fs = require('fs');
const assert = require('assert');

const AEC3Module = require('./dist/aec3_wasm.js');

const kSr = 16000;
const kBlock = 64;
const kBlocksPerSec = kSr / kBlock;

function rd32le(buf, off) { return buf[off] | (buf[off+1]<<8) | (buf[off+2]<<16) | (buf[off+3]<<24); }
function rd16le(buf, off) { return buf[off] | (buf[off+1]<<8); }

function readWavPcm16Mono16k(path) {
  const buf = fs.readFileSync(path);
  if (buf.length < 44) throw new Error('Too small WAV');
  if (buf.toString('utf8', 0, 4) !== 'RIFF' || buf.toString('utf8', 8, 12) !== 'WAVE') throw new Error('Not RIFF/WAVE');
  let pos = 12; let sr = 0, ch = 0, bps = 0; let dataOff = 0, dataSize = 0;
  while (pos + 8 <= buf.length) {
    const id = rd32le(buf, pos); pos += 4; const sz = rd32le(buf, pos); pos += 4; const start = pos;
    if (id === 0x20746d66) {
      const fmt = rd16le(buf, start+0); ch = rd16le(buf, start+2); sr = rd32le(buf, start+4); bps = rd16le(buf, start+14);
      if (fmt !== 1 || bps !== 16) throw new Error('Expected PCM16');
    } else if (id === 0x61746164) {
      dataOff = start; dataSize = sz; break;
    }
    pos = start + sz;
  }
  if (!dataOff || !dataSize) throw new Error('No data chunk');
  if (sr !== kSr || ch !== 1) throw new Error('Expected 16k mono wav');
  const ns = Math.floor(dataSize / 2);
  const arr = new Int16Array(ns);
  for (let i = 0; i < ns; i++) arr[i] = buf.readInt16LE(dataOff + i*2);
  return arr;
}

function writeWavPcm16Mono16k(path, samples) {
  const ch = 1, bps = 16, sr = kSr;
  const dataBytes = samples.length * 2;
  const byteRate = sr * ch * (bps/8);
  const blockAlign = ch * (bps/8);
  const out = Buffer.alloc(44 + dataBytes);
  out.write('RIFF', 0);
  out.writeUInt32LE(36 + dataBytes, 4);
  out.write('WAVE', 8);
  out.write('fmt ', 12);
  out.writeUInt32LE(16, 16);
  out.writeUInt16LE(1, 20);
  out.writeUInt16LE(ch, 22);
  out.writeUInt32LE(sr, 24);
  out.writeUInt32LE(byteRate, 28);
  out.writeUInt16LE(blockAlign, 32);
  out.writeUInt16LE(bps, 34);
  out.write('data', 36);
  out.writeUInt32LE(dataBytes, 40);
  for (let i = 0; i < samples.length; i++) out.writeInt16LE(samples[i], 44 + i*2);
  fs.writeFileSync(path, out);
}

function pushArray(dstArr, src) { for (let i = 0; i < src.length; i++) dstArr.push(src[i]|0); }
function popBlockI16(arr) {
  if (arr.length < kBlock) return null;
  const out = new Int16Array(kBlock);
  for (let i = 0; i < kBlock; i++) out[i] = arr.shift();
  return out;
}

function toInt16Array(x) {
  if (!x) return new Int16Array();
  if (ArrayBuffer.isView(x)) {
    if (x instanceof Int16Array) return x;
    return new Int16Array(x.buffer, x.byteOffset, Math.floor(x.byteLength / 2));
  }
  if (Buffer.isBuffer(x)) {
    return new Int16Array(x.buffer, x.byteOffset, Math.floor(x.byteLength / 2));
  }
  const out = new Int16Array(x.length);
  for (let i = 0; i < x.length; i++) out[i] = x[i]|0;
  return out;
}

(async () => {
  const args = process.argv.slice(2);
  if (args.length < 1) {
    console.error('Usage: node cancel_live.js <render.wav> [--no-linear] [--no-nonlinear]');
    process.exit(1);
  }
  const renderPath = args[0];
  let enableLinear = true;
  let enableNonlinear = true;
  for (const a of args.slice(1)) {
    if (a === '--no-linear') enableLinear = false;
    else if (a === '--no-nonlinear') enableNonlinear = false;
    else console.error('Unknown arg:', a);
  }

  const far = readWavPcm16Mono16k(renderPath);
  const totalBlocks = Math.ceil(far.length / kBlock);

  let PortAudio = null;
  if (process.platform === 'darwin') PortAudio = require('./PAmac.node');
  else {
    console.error('Only macOS (PAmac.node) is supported.');
    process.exit(1);
  }

  PortAudio.initSampleBuffers(kSr, kSr, kBlock);
  PortAudio.startMic();
  PortAudio.startSpeaker();

  const mod = await AEC3Module();
  const h = mod._aec3_create();
  assert(h !== 0);
  mod._aec3_set_modes(h, enableLinear ? 1 : 0, enableNonlinear ? 1 : 0);

  const bytes = kBlock * 2;
  const pRef = mod._malloc(bytes);
  const pCap = mod._malloc(bytes);
  const pOut = mod._malloc(bytes);

  let recQ = [];
  let refQ = [];
  let processed = [];
  let playIdx = 0;
  let blocksSent = 0;
  let donePlaying = false;
  let refBlocksProcessed = 0;
  let finished = false;
  let blockCounter = 0;
  let postPlaybackTicks = 0;
  const maxPostPlaybackTicks = kBlocksPerSec * 2;

  function shutdown() {
    if (typeof PortAudio.stopMic === 'function') PortAudio.stopMic();
    if (typeof PortAudio.stopSpeaker === 'function') PortAudio.stopSpeaker();
    mod._free(pRef); mod._free(pCap); mod._free(pOut);
    mod._aec3_destroy(h);
  }

  function processAvailableBlocks() {
    let processedAny = false;
    while (refQ.length >= kBlock && recQ.length >= kBlock) {
      const ref = popBlockI16(refQ);
      const cap = popBlockI16(recQ);
      mod.HEAP16.set(ref, pRef >> 1);
      mod.HEAP16.set(cap, pCap >> 1);
      mod._aec3_analyze(h, pRef);
      mod._aec3_process(h, pCap, pOut);
      const outView = mod.HEAP16.subarray(pOut >> 1, (pOut >> 1) + kBlock);
      const outBlock = new Int16Array(kBlock);
      outBlock.set(outView);
      for (let i = 0; i < kBlock; i++) processed.push(outBlock[i]);

      let y2 = 0, e2 = 0;
      for (let i = 0; i < kBlock; i++) { const yy = cap[i]; const ee = outBlock[i]; y2 += yy*yy; e2 += ee*ee; }
      const ratio = y2 > 0 ? (e2 / y2) : 0;
      const dblk = mod._aec3_get_estimated_delay_blocks(h);
      const dms = dblk >= 0 ? (dblk * (1000.0 * kBlock / kSr)) : -1;
      console.log(`block=${blockCounter} y2=${y2.toExponential()} e2=${e2.toExponential()} e2_over_y2=${ratio.toExponential()} est_delay_blocks=${dblk} est_delay_ms=${dms.toFixed(3)}`);
      blockCounter += 1;
      refBlocksProcessed += 1;
      processedAny = true;
    }
    return processedAny;
  }

  function finishIfDone() {
    if (!donePlaying) return;
    if (refBlocksProcessed < totalBlocks) return;
    if (recQ.length >= kBlock && postPlaybackTicks < maxPostPlaybackTicks) return;
    if (finished) return;
    finished = true;
    clearInterval(timer);
    shutdown();
    const totalSamples = refBlocksProcessed * kBlock;
    const outSamples = new Int16Array(totalSamples);
    for (let i = 0; i < totalSamples; i++) outSamples[i] = processed[i] || 0;
    writeWavPcm16Mono16k('processed.wav', outSamples);
    console.error(`processed.wav written (${totalSamples} samples).`);
    process.exit(0);
  }

  const tickMs = Math.floor(1000 * kBlock / kSr);
  const timer = setInterval(() => {
    if (!donePlaying && blocksSent < totalBlocks) {
      const block = new Int16Array(kBlock);
      const remain = far.length - playIdx;
      const copyCount = remain >= kBlock ? kBlock : remain;
      if (copyCount > 0) {
        const slice = far.subarray(playIdx, playIdx + copyCount);
        block.set(slice);
      }
      if (copyCount < kBlock) {
        for (let i = copyCount; i < kBlock; i++) block[i] = 0;
      }
      playIdx += copyCount;
      blocksSent += 1;
      pushArray(refQ, block);
      PortAudio.pushSamplesForPlay(block);
      if (blocksSent >= totalBlocks) donePlaying = true;
    } else {
      postPlaybackTicks += 1;
    }

    const recBuf = PortAudio.getRecordedSamples();
    const recArr = toInt16Array(recBuf);
    if (recArr.length > 0) {
      pushArray(recQ, recArr);
      if (typeof PortAudio.discardRecordedSamples === 'function') PortAudio.discardRecordedSamples(recArr.length);
      postPlaybackTicks = 0;
    }

    processAvailableBlocks();
    finishIfDone();
  }, tickMs);

  process.on('SIGINT', () => {
    if (finished) process.exit(0);
    finished = true;
    clearInterval(timer);
    shutdown();
    const totalSamples = refBlocksProcessed * kBlock;
    const outSamples = new Int16Array(totalSamples);
    for (let i = 0; i < totalSamples; i++) outSamples[i] = processed[i] || 0;
    writeWavPcm16Mono16k('processed.wav', outSamples);
    console.error('stopped. processed.wav written.');
    process.exit(0);
  });
})();
