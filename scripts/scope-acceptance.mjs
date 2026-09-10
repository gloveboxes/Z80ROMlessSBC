import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { SerialPort } from 'serialport';
import { parseArgs } from 'node:util';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { pipeline } from 'node:stream/promises';
import { finished } from 'node:stream/promises';
import { spawn, execFileSync } from 'node:child_process';
import { createInterface } from 'node:readline/promises';
import { stdin, stdout } from 'node:process';
import { join, basename, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { numericMeasurement, parseStatus, evaluateClock, readEdges, evaluateTrap } from './scope-evidence.mjs';

const { values } = parseArgs({ options: {
  help: { type: 'boolean' }, mode: { type: 'string' }, runtime: { type: 'string', default: 'container' },
  ip: { type: 'string' }, port: { type: 'string' }, output: { type: 'string' },
  'exclusive-session': { type: 'boolean' }, 'bench-confirmed': { type: 'boolean' },
  duration: { type: 'string', default: '10' }, captures: { type: 'string', default: '3' },
  'minimum-high-ns': { type: 'string' }, 'minimum-low-ns': { type: 'string' },
  'maximum-last-edge-us': { type: 'string' }, 'maximum-pause-us': { type: 'string' },
} });
if (values.help) {
  console.log(`Opt-in Z80 bench acceptance. Never run beside a registered scope server.
node scripts/scope-acceptance.mjs --mode stage2|stage8|spi --ip ADDRESS
  --port /dev/cu.usbmodem... --runtime container|docker --output NEW_DIRECTORY
  --exclusive-session --bench-confirmed
Stage 8 also requires datasheet/bench limits:
  --minimum-high-ns N --minimum-low-ns N --maximum-last-edge-us N --maximum-pause-us N
  --duration SECONDS (10..3600) --captures COUNT (1..20)
Stage 2: CH1=GP2 (3.3V), CH2=Z80 socket CLK (5V), CH3=Z80 VCC.
Stage 8: CH1=Z80 IORQ#, CH2=Z80 WAIT#, CH3=Z80 CLK, CH4=Pico DATA_ENABLE.
SPI: CH1=MCP SCK, CH2=MCP CS#, CH3=MCP SI, CH4=MCP SO (all 5V side).
Firmware identity must match stage2 or stage8 (SPI uses Stage 8).
All probes: compensated 10X, grounded; inputs 1 MOhm, deskew checked.
The runner does not flash firmware or inject electrical faults.`);
  process.exit(0);
}
if (!['stage2', 'stage8', 'spi'].includes(values.mode) || !['container', 'docker'].includes(values.runtime) ||
    !values.ip || !values.port || !values.output || !values['exclusive-session'] || !values['bench-confirmed']) {
  throw new Error('Read --help; supply mode, runtime, IP, port, new output directory and both safety confirmations');
}
const duration = Number(values.duration);
const captureCount = Number(values.captures);
if (!Number.isFinite(duration) || duration < 10 || duration > 3600 ||
    !Number.isInteger(captureCount) || captureCount < 1 || captureCount > 20) {
  throw new Error('duration must be 10..3600 seconds; captures must be 1..20');
}
const limits = {
  minimum_high_s: Number(values['minimum-high-ns']) * 1e-9,
  minimum_low_s: Number(values['minimum-low-ns']) * 1e-9,
  maximum_last_edge_latency_s: Number(values['maximum-last-edge-us']) * 1e-6,
  maximum_pause_s: Number(values['maximum-pause-us']) * 1e-6,
};
if (values.mode === 'stage8' && Object.values(limits).some(value => !Number.isFinite(value) || value <= 0)) {
  throw new Error('Stage 8 requires all four positive timing limits; do not guess datasheet limits');
}
const output = resolve(values.output);
await mkdir(output, { recursive: false });
const report = {
  started_at: new Date().toISOString(), mode: values.mode, qualified: false,
  automated_checks_passed: false,
  firmware_revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
  firmware_dirty: Boolean(execFileSync('git', ['status', '--porcelain'], { encoding: 'utf8' }).trim()),
  limits: values.mode === 'stage8' ? limits : undefined,
  operator_confirmations: ['exclusive instrument connection', 'probe wiring, grounding, ratio and voltage domains checked'],
  calls: [], cases: [], errors: [], restoration: { verified: false },
};
const sessionName = `z80-acceptance-${process.pid}-${Date.now()}`;
const runtimeArgs = values.runtime === 'docker'
  ? ['run', '--rm', '-i', '--name', sessionName, '--read-only', '--tmpfs', '/tmp', '--cap-drop=ALL',
    '--security-opt=no-new-privileges', '-e', `RIGOL_IP=${values.ip}`,
    '--mount', 'type=volume,src=rigol-mcp-data,dst=/data', 'rigol-mcp:local']
  : ['run', '--rm', '-i', '--name', sessionName, '--read-only', '--progress', 'none', '--tmpfs', '/tmp',
    '--cap-drop', 'ALL', '-e', `RIGOL_IP=${values.ip}`,
    '--mount', 'type=volume,source=rigol-mcp-data,target=/data', 'rigol-mcp:local'];
const client = new Client({ name: 'z80-bench-acceptance', version: '1.0.0' });
const transport = new StdioClientTransport({ command: values.runtime, args: runtimeArgs, stderr: 'inherit' });
const terminal = createInterface({ input: stdin, output: stdout });
let serial;
let serialText = '';
let serialFailure;
let initial;
let snapshot;
let firmwareVerified = false;
let initialSweep;
let hourPass = false;
const serialLog = createWriteStream(join(output, 'serial.log'));
serialLog.on('error', error => { serialFailure = error; });
const transferSettings = {};

async function copyArtifact(remote) {
  if (!/^\/data\/(captures|screenshots)\/[A-Za-z0-9_.-]+$/.test(remote)) throw new Error('Unexpected artifact path');
  const local = join(output, basename(remote));
  const child = spawn(values.runtime, ['exec', sessionName, 'cat', remote], { stdio: ['ignore', 'pipe', 'inherit'] });
  const exited = new Promise((accept, reject) => {
    child.on('error', reject);
    child.on('exit', code => code === 0 ? accept() : reject(new Error(`Artifact copy exited ${code}`)));
  });
  await Promise.all([pipeline(child.stdout, createWriteStream(local)), exited]);
  return local;
}
async function call(name, args = {}) {
  const entry = { name, arguments: { ...args }, started_at: new Date().toISOString() };
  if (entry.arguments.confirm_token) entry.arguments.confirm_token = '<redacted>';
  report.calls.push(entry);
  try {
    const response = await client.callTool({ name, arguments: args }, undefined, { timeout: 300000 });
    const text = response.content.filter(block => block.type === 'text').map(block => block.text).join('\n');
    if (response.isError) throw new Error(text);
    let result;
    try { result = JSON.parse(text); } catch { result = text; }
    if (result.file_backed) result = JSON.parse(await readFile(await copyArtifact(result.path), 'utf8'));
    entry.result = result?.confirm_token ? { ...result, confirm_token: '<redacted>' } : result;
    return result;
  } catch (error) {
    entry.error = error.message;
    throw error;
  }
}
async function query(command) {
  return (await call('scpi_execute', { command, operation: 'query' })).value;
}
async function command(text, expression, timeout = 10000) {
  serialText = '';
  await new Promise((accept, reject) => serial.write(text, error => error ? reject(error) : accept()));
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (serialFailure) throw serialFailure;
    const match = serialText.match(expression);
    if (match) return match[0];
    await delay(20);
  }
  throw new Error(`Firmware response timeout for ${JSON.stringify(text)}`);
}
function requireApplied(result) {
  if (result.fully_applied !== true) throw new Error(`Configuration readback mismatch: ${JSON.stringify(result.mismatches)}`);
}
async function disableAnalysis() {
  requireApplied(await call('configure_recording', { enabled: false }));
  requireApplied(await call('configure_mask_test', { enabled: false }));
  for (let index = 1; index <= 4; ++index) {
    await call('scpi_execute', { command: `:BUS${index}:DISPlay`, operation: 'write', arguments: [false] });
    if (await query(`:BUS${index}:DISPlay`) !== '0') throw new Error('Decoder display did not disable');
    requireApplied(await call('configure_math', { math_channel: index, display: false }));
  }
}
function channel(number, label, domain) {
  return { channel: number, label, voltage_domain_v: domain, scale_v_div: domain === 5 ? 2 : 1,
    probe_ratio: 10, coupling: 'DC', bandwidth_limit: 'OFF' };
}
async function configure(channels, trigger, scale) {
  requireApplied(await call('configure_timing_capture', { channels, trigger, time_scale_s_div: scale,
    acquisition_type: 'NORMAL', memory_depth: 'AUTO', disable_unlisted: true }));
}
async function stoppedCapture(channels, label) {
  const capture = await call('acquire_and_capture', { channels, label, timeout_s: 10 });
  await copyArtifact(capture.path);
  return capture;
}

try {
  await client.connect(transport);
  report.identity = await call('idn');
  const capabilities = await call('get_capabilities');
  if (capabilities.model !== 'DHO814') throw new Error('This bench plan targets DHO814 only');
  initial = await call('get_scope_state');
  report.initial_state = initial;
  initialSweep = await query(':TRIGger:SWEep');
  if (await call('check_error') !== 'No error') throw new Error('Initial SCPI error queue is not empty');
  snapshot = await call('save_scope_setup');
  report.saved_setup = snapshot;
  await copyArtifact(snapshot.path);
  if (snapshot.state_path) await copyArtifact(snapshot.state_path);
  for (const field of ['SOURce', 'MODE', 'FORMat', 'STARt', 'STOP']) {
    transferSettings[field] = await query(`:WAVeform:${field}`);
  }
  serial = new SerialPort({ path: values.port, baudRate: 115200, autoOpen: false });
  serial.on('data', data => {
    serialLog.write(data);
    serialText = (serialText + data.toString('utf8')).slice(-131072);
    hourPass ||= /PASS: one-hour RAM\/USB test/.test(serialText);
  });
  serial.on('error', error => { serialFailure = error; });
  await new Promise((accept, reject) => serial.open(error => error ? reject(error) : accept()));
  const stage = values.mode === 'stage2' ? 2 : 8;
  const statusCommand = stage === 2 ? 's' : '\x1ds';
  report.initial_firmware = parseStatus(await command(statusCommand, /stage=\d+[^\r\n]*\n/), stage);
  firmwareVerified = true;
  await disableAnalysis();
  const annotation = await terminal.question('Record board revision, flashed UF2 identity, probe IDs/deskew and datasheet limit references: ');
  if (!annotation.trim()) throw new Error('Bench provenance is required');
  report.bench_provenance = annotation.trim();
  if (stage === 2) {
    for (const [key, requested] of [['1', 1000], ['2', 100000], ['3', 1000000]]) {
      const firmware = parseStatus(await command(key, /stage=2[^\r\n]*\n/), 2);
      if (firmware.clock_requested !== requested) throw new Error('Unexpected firmware clock request');
      await configure([channel(1, 'Pico GP2 CLK', 3.3), channel(2, 'Z80 socket CLK', 5), channel(3, 'Z80 VCC', 5)],
        { channel: 1, slope: 'POS', level_v: 1.65 }, 4 / firmware.clock_actual / 10);
      await call('run');
      const measurements = { CHAN1: {}, CHAN2: {}, CHAN3: {} };
      for (const source of ['CHAN1', 'CHAN2']) {
        for (const item of ['FREQUENCY', 'PDUTY', 'PWIDTH', 'NWIDTH', 'VMIN', 'VMAX']) {
          measurements[source][item] = numericMeasurement(await call('measure', { channel: source, item }));
        }
      }
      measurements.CHAN3.VAVG = numericMeasurement(await call('measure', { channel: 'CHAN3', item: 'VAVG' }));
      const capture = await stoppedCapture(['CHAN1', 'CHAN2', 'CHAN3'], `stage2_${requested}`);
      const evaluation = evaluateClock(firmware.clock_actual, measurements, capture.channels);
      report.cases.push({ firmware, measurements, capture, evaluation });
      if (!evaluation.automated_checks_passed) throw new Error('Stage 2 evidence failed or is inconclusive; do not increase frequency');
    }
  } else {
    const baseline = report.initial_firmware;
    if (!(baseline.clock_actual > 0)) throw new Error('Stage 8 clock is not configured');
    if (values.mode === 'stage8') {
      await configure([channel(1, 'Z80 IORQ#', 5), channel(2, 'Z80 WAIT#', 5),
        channel(3, 'Z80 CLK', 5), channel(4, 'Pico DATA_ENABLE', 3.3)],
      { channel: 1, slope: 'NEG', level_v: 2.5 }, 100e-6);
      const started = Date.now();
      await command('\x1dh', /\[diag\] one-hour RAM\/USB test started/);
      const baselineAfterStart = parseStatus(await command(statusCommand, /stage=8[^\r\n]*\n/), 8);
      for (let index = 0; index < captureCount; ++index) {
        await delay(Math.max(0, started + duration * 1000 * (index + 1) / captureCount - Date.now()));
        const capture = await stoppedCapture(['CHAN1', 'CHAN2', 'CHAN3', 'CHAN4'], `stage8_trap_${index}`);
        for (const source of ['CHAN1', 'CHAN2', 'CHAN3', 'CHAN4']) {
          if (capture.channels[source]?.valid !== true || capture.channels[source]?.clipped !== false) {
            throw new Error(`Invalid or clipped capture: ${source}`);
          }
        }
        const traces = {};
        const raw = {};
        for (const source of ['CHAN1', 'CHAN2', 'CHAN3', 'CHAN4']) {
          const transfer = await call('download_waveform', { source, mode: 'RAW' });
          const local = await copyArtifact(transfer.path);
          if (transfer.invalid_samples !== 0) throw new Error('RAW transfer contains invalid samples');
          traces[source] = await readEdges(local, source === 'CHAN4' ? 1.65 : 2.5);
          if (traces[source].points !== transfer.points) throw new Error('Incomplete RAW artifact');
          raw[source] = transfer;
        }
        const status = parseStatus(await command(statusCommand, /stage=8[^\r\n]*\n/), 8);
        for (const counter of ['boots', 'dma_fail', 'verify_fail', 'ram_fail', 'trap_timeout', 'control_error', 'rx_drop', 'tx_drop']) {
          if (!Number.isFinite(status[counter]) || status[counter] !== baselineAfterStart[counter]) {
            throw new Error(`Functional counter changed: ${counter}`);
          }
        }
        if (status.clock_actual !== baseline.clock_actual) throw new Error('Clock changed during test');
        const evaluation = evaluateTrap(traces, status.clock_actual, limits);
        const traceMetadata = Object.fromEntries(Object.entries(traces).map(([source, trace]) => {
          const { edges, ...metadata } = trace;
          return [source, { ...metadata, edge_count: edges.length, threshold_v: source === 'CHAN4' ? 1.65 : 2.5 }];
        }));
        report.cases.push({ capture, raw, trace_metadata: traceMetadata, status, evaluation });
        if (!evaluation.automated_checks_passed) throw new Error('I/O timing evidence failed or is inconclusive');
      }
      report.one_hour_firmware_pass = hourPass;
      if (duration === 3600 && !hourPass) throw new Error('One-hour firmware PASS was not observed');
    } else {
      await configure([channel(1, 'MCP SCK', 5), channel(2, 'MCP CS#', 5),
        channel(3, 'MCP SI', 5), channel(4, 'MCP SO', 5)],
      { channel: 2, slope: 'NEG', level_v: 2.5 }, 20e-6);
      requireApplied(await call('configure_decode', { bus: 1, protocol: 'SPI', display: true, format: 'HEX',
        settings: { clock: 'CHAN1', clock_slope: 'POSITIVE', mosi: 'CHAN3', miso: 'CHAN4',
          mosi_polarity: 'HIGH', miso_polarity: 'HIGH', endian: 'MSB', data_bits: 8,
          cs_mode: 'CS', chip_select: 'CHAN2', chip_select_polarity: 'LOW',
          thresholds_v: { CLK: 2.5, CS: 2.5, MOSI: 2.5, MISO: 2.5 } } }));
      await command('\x1dr', /\[diag\] RAM\/USB checker started/);
      await call('run');
      await delay(500);
      await call('stop');
      report.decoder = await call('get_decode_result', { bus: 1 });
      await writeFile(join(output, 'spi-decode.json'), JSON.stringify(report.decoder, null, 2));
      requireApplied(await call('configure_decode', { bus: 1, protocol: 'SPI', display: false }));
      const capture = await call('capture_waveforms', { channels: ['CHAN1', 'CHAN2', 'CHAN3', 'CHAN4'], label: 'mcp23s17_spi' });
      await copyArtifact(capture.path);
      report.cases.push({ capture, status: 'manual_review_required', manual_checks_pending: [
        'Correlate CS-framed 40/41 opcodes, IODIRA/B 00/01, GPIOA 12 and OLATA/B 14/15 with firmware operations.',
        'For reads ignore simultaneous dummy MOSI; compare third MISO byte to the preceding direction write.',
        'Check actual SCK rate, CS setup/hold and all receiver voltage margins; preserve DSLogic evidence.',
      ] });
    }
  }
} catch (error) {
  report.errors.push(error.message);
  console.error(error.message);
} finally {
  if (serial?.isOpen) {
    if (firmwareVerified) {
      try {
        await command(values.mode === 'stage2' ? 'x' : '\x1dx', /clock stopped|CPU held in reset/);
        report.firmware_exit = values.mode === 'stage2' ? 'clock stopped' : 'CPU held in reset';
      } catch (error) { report.errors.push(`Firmware cleanup: ${error.message}`); }
    }
    await new Promise(accept => serial.close(() => accept()));
  }
  if (snapshot) {
    try {
      await call('stop');
      const answer = await terminal.question('Restore saved scope setup and transfer settings? Type restore: ');
      if (answer.trim() === 'restore') {
        const confirmation = await call('restore_scope_setup', { path: snapshot.path });
        if (!confirmation.confirm_token) throw new Error('Missing restore confirmation token');
        report.restoration = await call('restore_scope_setup', { path: snapshot.path, confirm_token: confirmation.confirm_token });
        for (const field of ['SOURce', 'MODE', 'FORMat', 'STARt', 'STOP']) {
          if (transferSettings[field] !== undefined) {
            await call('scpi_execute', { command: `:WAVeform:${field}`, operation: 'write', arguments: [transferSettings[field]] });
            const applied = await query(`:WAVeform:${field}`);
            if (applied !== transferSettings[field]) throw new Error(`Transfer restore mismatch: ${field}`);
          }
        }
        if (initial.trigger.status !== 'STOP') {
          if (initialSweep === 'SING' || initialSweep === 'SINGLE') await call('single');
          else await call('run');
        }
        report.restoration.verified = report.restoration.verification?.verified === true;
      } else report.restoration = { verified: false, reason: 'Operator declined restoration; scope left stopped' };
      report.final_state = await call('get_scope_state');
      if (initial.trigger.status === 'STOP' && report.final_state.trigger.status !== 'STOP') {
        report.restoration.verified = false;
        report.errors.push('Scope did not return to its original STOP state');
      }
      report.final_error = await call('check_error');
    } catch (error) { report.errors.push(`Scope cleanup: ${error.message}`); }
  }
  terminal.close();
  await client.close().catch(error => report.errors.push(`Client close: ${error.message}`));
  report.finished_at = new Date().toISOString();
  report.automated_checks_passed = report.errors.length === 0 && report.cases.length > 0 &&
    report.cases.every(entry => entry.evaluation?.automated_checks_passed === true);
  serialLog.end();
  await finished(serialLog);
  await writeFile(join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}
console.log(`Evidence saved under ${output}; qualified=false until all manual gates pass.`);
if (report.errors.length || !report.restoration.verified || report.final_error !== 'No error') process.exitCode = 1;