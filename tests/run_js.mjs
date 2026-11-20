// Ejecuta el prototipo ORIGINAL (web/vp330-emulator.html) en Node, con una implementación
// de Web Audio (node-web-audio-api) y un DOM simulado, y graba lo que suena en cada escenario.
// compare_cpp hace lo mismo con el motor en C++ y compare.mjs compara ambos.
//
//   npm install node-web-audio-api   (una vez)
//   node tests/run_js.mjs <carpeta-salida>
//
// Las acciones se hacen exactamente como un usuario: pulsar teclas del teclado en pantalla,
// mover faders (evento input) y pulsar tablets (evento click). El micrófono se sustituye por
// una voz sintética.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { OfflineAudioContext } = require('node-web-audio-api');

const here = path.dirname(fileURLToPath(import.meta.url));
const outDir = process.argv[2] || path.join(here, 'out');
fs.mkdirSync(outDir, { recursive: true });
const SR = 48000, Q = 128;

// ── escenarios (en quantums de 128 muestras) ──
const scenarios = process.env.DBG ? {
  dbg_micdry: `
q 2 click toggleMic
q 3 slider balanceMic 5
q 300 end`,
  dbg_voc: `
q 0 click toggleVocoderLower
q 0 click toggleStringsLower
q 0 click toggleChoirLower
q 2 click toggleMic
q 5 on 48
q 300 end`,
  dbg_voc_nonoise: `
q 0 click toggleVocoderLower
q 0 click toggleStringsLower
q 0 click toggleChoirLower
q 0 slider noisemix 0
q 2 click toggleMic
q 5 on 48
q 300 end`,
} : {
  strings_choir: `
q 0 slider balanceStrings 8
q 4 on 48
q 4 on 55
q 40 on 64
q 40 on 67
q 300 off 48
q 300 off 55
q 330 off 64
q 330 off 67
q 520 end`,
  strings_tone_env: `
q 0 click toggleChoirLower
q 0 click toggleChoirUpper
q 0 slider balanceStrings 9
q 0 slider tone 40
q 0 slider stringsAttack 300
q 0 slider release 800
q 0 slider ensemble 0
q 5 on 60
q 120 slider ensemble 90
q 200 slider tone 80
q 300 off 60
q 310 on 62
q 330 off 62
q 700 end`,
  voices_vibrato: `
q 0 click toggleStringsLower
q 0 click toggleStringsUpper
q 0 slider balanceChoir 9
q 0 click voiceLowerMale4
q 0 click voiceUpperFemale4
q 0 slider attack 150
q 0 slider vibratoDelay 80
q 0 slider vibratoRate 6.5
q 5 on 50
q 5 on 69
q 150 slider vibratoDepth 9
q 200 slider lowerVolume 4
q 260 click voiceLowerMale8
q 300 off 50
q 300 off 69
q 302 on 52
q 380 off 52
q 560 end`,
  pitch_auto: `
q 0 slider balanceStrings 8
q 0 click pitchModeAuto
q 0 slider pitchSet 7
q 0 slider pitchTime 300
q 5 on 57
q 5 on 64
q 200 off 57
q 200 off 64
q 210 slider pitchTune 30
q 215 on 60
q 330 off 60
q 480 end`,
  pitch_manual: `
q 0 slider balanceStrings 8
q 0 click pitchModeManual
q 5 on 60
q 5 on 72
q 100 slider pitchShift 50
q 160 slider pitchSet 12
q 220 slider pitchShift 0
q 300 off 60
q 300 off 72
q 450 end`,
  vocoder: `
q 0 slider balanceStrings 8
q 0 click toggleVocoderLower
q 0 click toggleVocoderUpper
q 0 click toggleStringsLower
q 0 click toggleStringsUpper
q 0 click toggleChoirLower
q 0 click toggleChoirUpper
q 2 click toggleMic
q 3 slider balanceMic 2
q 5 on 48
q 5 on 60
q 5 on 64
q 200 slider vocoderTone 45
q 260 slider sens 9
q 300 slider noisegate 4
q 340 slider noisemix 35
q 380 click toggleVocoderEnsemble
q 420 slider vocoderMicLevel 8
q 600 off 48
q 600 off 60
q 600 off 64
q 760 end`,
  fx_split: `
q 0 slider balanceStrings 8
q 0 slider splitPoint 0
q 0 slider delayTime 250
q 0 slider delayFeedback 60
q 0 slider delayMix 40
q 0 slider reverbSize 1.5
q 0 slider reverbTone 3000
q 0 slider reverbMix 50
q 5 on 55
q 60 off 55
q 70 on 62
q 130 off 62
q 140 slider delayTime 400
q 150 on 67
q 220 off 67
q 230 click toggleChoirEnsemble
q 240 on 59
q 300 off 59
q 900 end`,
  loud_limiter: `
q 0 slider balanceStrings 10
q 0 slider balanceChoir 10
q 0 slider master 100
q 0 slider upperVolume 10
q 5 on 48
q 5 on 52
q 5 on 55
q 5 on 60
q 5 on 64
q 5 on 67
q 5 on 72
q 250 slider master 40
q 300 off 48
q 300 off 52
q 300 off 55
q 300 off 60
q 300 off 64
q 300 off 67
q 300 off 72
q 450 end`,
};

// ── voz sintética para el micrófono (vocal "a" con vibrato y una "s") ──
function makeVoice(n) {
  const out = new Float32Array(n);
  const bq = (f, q) => { const w = 2 * Math.PI * f / SR, a = Math.sin(w) / (2 * q), c = Math.cos(w); return { b0: a / (1 + a), b2: -a / (1 + a), a1: -2 * c / (1 + a), a2: (1 - a) / (1 + a), x1: 0, x2: 0, y1: 0, y2: 0 }; };
  const run = (f, x) => { const y = f.b0 * x + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2; f.x2 = f.x1; f.x1 = x; f.y2 = f.y1; f.y1 = y; return y; };
  const fs = [bq(700, 8), bq(1220, 10), bq(2600, 12)];
  let ph = 0, s = 12345;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    const f0 = 115 * (1 + 0.02 * Math.sin(2 * Math.PI * 5 * t));
    ph += f0 / SR; let pulse = 0; if (ph >= 1) { ph -= 1; pulse = 1; }
    const env = Math.min(1, t * 8) * (t > 1.2 && t < 1.45 ? 0.1 : 1);
    let v = (run(fs[0], pulse) * 1.0 + run(fs[1], pulse) * 0.6 + run(fs[2], pulse) * 0.3) * 6 * env;
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    if (t > 1.2 && t < 1.45) v += (s / 4294967296 - 0.5) * 0.25;  // fricativa
    out[i] = v;
  }
  return out;
}
const micSignal = makeVoice(SR * 20);
fs.writeFileSync(path.join(outDir, 'mic.f32'), Buffer.from(micSignal.buffer));

// ── carga del script original ──
const html = fs.readFileSync(path.join(here, '..', 'web', 'vp330-emulator.html'), 'utf8');
const scriptSrc = html.slice(html.lastIndexOf('<script>') + 8, html.lastIndexOf('</script>'));

function lcg(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }

async function runScenario(name, text) {
  const lines = text.trim().split('\n').map((l) => l.trim().split(/\s+/));
  const endQ = +lines.find((l) => l[2] === 'end')[1];
  const LEN = endQ * Q;

  // DOM simulado
  const els = new Map(), created = [], winListeners = {};
  const classList = () => { const s = new Set(); return { add: (c) => s.add(c), remove: (c) => s.delete(c), toggle: (c, on) => { const v = on === undefined ? !s.has(c) : on; v ? s.add(c) : s.delete(c); return v; }, contains: (c) => s.has(c) }; };
  const makeEl = (id) => ({ id, listeners: {}, classList: classList(), style: {}, dataset: {}, textContent: '', innerHTML: '', value: '', min: '0', max: '1', disabled: false, options: [],
    addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }, appendChild(c) { return c; }, removeChild() {}, click() {} });
  const document = {
    getElementById: (id) => { if (!els.has(id)) els.set(id, makeEl(id)); return els.get(id); },
    createElement: () => { const e = makeEl(''); created.push(e); return e; },
    querySelector: (sel) => { const m = /data-midi="(\d+)"/.exec(sel); return m ? created.find((e) => String(e.dataset.midi) === m[1]) || null : null; },
    querySelectorAll: () => [],
    body: { appendChild() {}, removeChild() {} },
  };
  const holder = {};
  class TestContext extends OfflineAudioContext {
    constructor() { super(2, LEN, SR); holder.ctx = this; }
    // node-web-audio-api no deja cambiar la curva de un WaveShaper dos veces (Chrome sí, y
    // el prototipo lo hace al mover el noise gate): se envuelve en entrada→shaper→salida y
    // al cambiar la curva se sustituye el shaper por uno nuevo
    createWaveShaper() {
      const inNode = this.createGain(), outNode = this.createGain();
      let shaper = null;
      const ctx = this;
      const nativeConnect = outNode.connect.bind(outNode);
      Object.defineProperty(inNode, 'curve', { set(c) {
        const s2 = OfflineAudioContext.prototype.createWaveShaper.call(ctx);
        s2.curve = c; inNode.connect(s2); s2.connect(outNode);
        if (shaper) { inNode.disconnect(shaper); shaper.disconnect(); }
        shaper = s2;
      } });
      Object.defineProperty(inNode, 'oversample', { set() {} });
      const realConnect = inNode.connect.bind(inNode);
      inNode.connect = (dest, ...rest) => (dest && (dest === shaper || dest.constructor?.name === 'WaveShaperNode')) ? realConnect(dest, ...rest) : nativeConnect(dest, ...rest);
      return inNode;
    }
    createMediaStreamSource() {
      const b = this.createBuffer(1, micSignal.length, SR);
      b.copyToChannel(micSignal, 0);
      const src = this.createBufferSource(); src.buffer = b; src.start(this.currentTime);
      holder.micStartQ = Math.round(this.currentTime * SR / Q);
      return src;
    }
  }
  const M = Object.create(Math); M.random = lcg(1);
  const context = {
    document, Math: M, console, Float32Array, Uint8Array, Array, Object, Map, Set, Promise, Error, String, Number, Blob: class {}, URL: { createObjectURL() {}, revokeObjectURL() {} },
    window: { addEventListener: (t, f) => (winListeners[t] ||= []).push(f), AudioContext: TestContext },
    navigator: { mediaDevices: { enumerateDevices: async () => [], getUserMedia: async () => ({ getTracks: () => [] }), addEventListener() {} } },
    setInterval: () => 0, clearInterval() {}, setTimeout: () => 0, clearTimeout() {}, requestAnimationFrame: () => 0, alert: (m) => console.log('alert:', m),
    OfflineAudioContext, DataView, ArrayBuffer,
  };
  context.window.webkitAudioContext = TestContext;
  vm.createContext(context);
  vm.runInContext(scriptSrc, context);

  const fire = async (el, type, ev) => { for (const f of el.listeners[type] || []) await f(ev); };
  const pressPower = () => fire(document.getElementById('powerBtn'), 'click', { currentTarget: document.getElementById('powerBtn') });
  const act = async (l) => {
    const [, , cmd, a, b] = l;
    if (cmd === 'slider') { const el = document.getElementById(a); el.value = b; await fire(el, 'input', { target: el, currentTarget: el }); }
    else if (cmd === 'click') { const el = document.getElementById(a); await fire(el, 'click', { target: el, currentTarget: el }); }
    else if (cmd === 'on' || cmd === 'off') {
      const key = created.find((e) => String(e.dataset.midi) === a);
      if (!key) throw new Error('tecla fuera del teclado en pantalla: ' + a);
      await fire(key, cmd === 'on' ? 'mousedown' : 'mouseup', { preventDefault() {} });
    }
  };

  await pressPower();
  const ctx = holder.ctx;
  const byQ = new Map();
  for (const l of lines) { if (l[2] === 'end') continue; const q = +l[1]; (byQ.get(q) || byQ.set(q, []).get(q)).push(l); }
  for (const l of byQ.get(0) || []) await act(l);
  for (const [q, acts] of byQ) {
    if (q === 0) continue;
    ctx.suspend((q * Q - 0.5) / SR).catch((e) => console.log('  suspend falló en q', q, e.message, 'currentTime', ctx.currentTime)).then(async () => {
      const got = Math.round(ctx.currentTime * SR / Q);
      if (got !== q) console.log(`  aviso: ${name} q=${q} llegó en ${got}`);
      for (const l of acts) await act(l);
      ctx.resume();
    });
  }
  // node-web-audio-api registra las suspensiones de forma asíncrona: hay que dejar que
  // terminen de registrarse antes de empezar a renderizar
  await new Promise((r) => setTimeout(r, 50));
  const buf = await ctx.startRendering();
  const out = new Float32Array(LEN * 2);
  out.set(buf.getChannelData(0), 0); out.set(buf.getChannelData(1), LEN);
  fs.writeFileSync(path.join(outDir, name + '.js.f32'), Buffer.from(out.buffer));
  fs.writeFileSync(path.join(outDir, name + '.txt'), text.trim() + '\n' + (holder.micStartQ !== undefined ? `q ${holder.micStartQ} micstart\n` : ''));
}

for (const [name, text] of Object.entries(scenarios)) { process.stdout.write(name + ' '); await runScenario(name, text); console.log('ok'); }
fs.writeFileSync(path.join(outDir, 'scenarios.json'), JSON.stringify(Object.keys(scenarios)));
console.log('escenarios JS:', Object.keys(scenarios).length, '→', outDir);
