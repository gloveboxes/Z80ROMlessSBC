import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { numericMeasurement, parseStatus, evaluateClock, readEdges, evaluateTrap } from './scope-evidence.mjs';

test('reject unavailable hardware readings and wrong firmware identity', () => {
  for (const value of ['9.9000E+37', 'FREQUENCY on CHAN1: 9.9E37 invalid', 'NaN', null, '']) {
    assert.equal(numericMeasurement(value), null);
  }
  assert.equal(numericMeasurement('FREQUENCY on CHAN1: 1.000E6'), 1e6);
  assert.throws(() => parseStatus('stage=8 clock_actual=1000000', 2));
  assert.equal(parseStatus('stage=2 clock_actual=1000000', 2).clock_actual, 1e6);
});

test('steady clock uses settled rails and never claims full phase qualification', () => {
  const measurements = { CHAN3: { VAVG: 5 } };
  const summaries = {};
  for (const source of ['CHAN1', 'CHAN2']) {
    measurements[source] = { FREQUENCY: 1e6, PDUTY: 50, PWIDTH: 0.5e-6, NWIDTH: 0.5e-6 };
    summaries[source] = { valid: true, clipped: false,
      statistics: { low_level_v: 0, high_level_v: source === 'CHAN1' ? 3.3 : 4.8, maximum_v: 6 } };
  }
  const result = evaluateClock(1e6, measurements, summaries);
  assert.equal(result.automated_checks_passed, true);
  assert.equal(result.qualified, false);
  measurements.CHAN2.PWIDTH = '9.9E37';
  assert.equal(evaluateClock(1e6, measurements, summaries).automated_checks_passed, false);
});

function trace(edges) {
  return { start: 0, end: 20e-6, points: 2001, sample_interval_s: 1e-8,
    edges: edges.map(([time, rising]) => ({ time: time * 1e-6, rising })) };
}
function capture() {
  return {
    CHAN1: trace([[2, false], [9, true]]),
    CHAN2: trace([[2.02, false], [7.9, true]]),
    CHAN3: trace([[0, true], [0.5, false], [1, true], [1.5, false], [2, true],
      [2.5, false], [8, true], [8.5, false], [9, true], [9.5, false]]),
    CHAN4: trace([[7.8, true], [9.1, false]]),
  };
}
const limits = { minimum_high_s: 0.4e-6, minimum_low_s: 0.4e-6,
  maximum_last_edge_latency_s: 1e-6, maximum_pause_s: 20e-6 };

test('intentional pause is separated from regular clock widths', () => {
  const result = evaluateTrap(capture(), 1e6, limits);
  assert.equal(result.automated_checks_passed, true);
  assert.equal(result.events.length, 1);
  assert.equal(result.qualified, false);
  assert.equal(result.intentional_pauses_excluded_from_period_jitter, true);
});
test('early WAIT release and short clock pulses fail', () => {
  const traces = capture();
  traces.CHAN4 = trace([[8.1, true], [9.1, false]]);
  assert.equal(evaluateTrap(traces, 1e6, limits).automated_checks_passed, false);
  traces.CHAN3.edges[1].time = 0.1e-6;
  assert.ok(evaluateTrap(traces, 1e6, limits).checks.some(check => check.status === 'fail'));
  const earlyDisable = capture();
  earlyDisable.CHAN4 = trace([[7.8, true], [8.2, false]]);
  assert.equal(evaluateTrap(earlyDisable, 1e6, limits).automated_checks_passed, false);
});
test('undersampled, incomplete, unaligned captures cannot pass', () => {
  const traces = capture();
  traces.CHAN1 = trace([[2, false]]);
  assert.equal(evaluateTrap(traces, 1e6, limits).automated_checks_passed, false);
  traces.CHAN1.start = 1;
  assert.throws(() => evaluateTrap(traces, 1e6, limits), /Unaligned/);
  traces.CHAN3.sample_interval_s = 1e-6;
  for (const value of Object.values(traces)) Object.assign(value, { start: 0, sample_interval_s: 1e-6 });
  assert.equal(evaluateTrap(traces, 1e6, limits).status, 'inconclusive');
});
test('stream CSV edges and reject invalid data', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'z80-evidence-'));
  try {
    const filename = join(directory, 'waveform.csv');
    await writeFile(filename, 'time_s,value\n0,0\n0.000001,3.3\n0.000002,0\n');
    const result = await readEdges(filename, 1.65);
    assert.equal(result.edges.length, 2);
    assert.equal(result.edges[0].time, 0.5e-6);
    await writeFile(filename, 'time_s,value\n0,NaN\n');
    await assert.rejects(readEdges(filename, 1.65), /Invalid sample/);
  } finally {
    await rm(directory, { recursive: true });
  }
});