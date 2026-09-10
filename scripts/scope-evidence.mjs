import { createReadStream } from 'node:fs';
import { parse } from 'csv-parse';

export function numericMeasurement(response) {
  if (typeof response === 'string' && response.includes(':')) {
    response = response.slice(response.indexOf(':') + 1).trim().split(/\s/)[0];
  }
  if (response === null || response === undefined || response === '') return null;
  const value = Number(response);
  return Number.isFinite(value) && Math.abs(value) < 9.9e37 ? value : null;
}

export function parseStatus(line, stage) {
  const fields = Object.fromEntries([...line.matchAll(/([a-z_]+)=(\d+)/g)]
    .map(([, key, value]) => [key, Number(value)]));
  if (fields.stage !== stage || !Number.isFinite(fields.clock_actual)) {
    throw new Error(`Expected stage=${stage} status with clock_actual`);
  }
  return fields;
}

export function checkRange(checks, name, value, minimum, maximum) {
  value = numericMeasurement(value);
  checks.push({ name, value, minimum, maximum,
    status: value === null ? 'inconclusive' : value >= minimum && value <= maximum ? 'pass' : 'fail' });
}

export function evaluateClock(actualHz, measurements, summaries) {
  if (!(actualHz > 0)) throw new Error('A configured actual frequency is required');
  const checks = [];
  const supply = numericMeasurement(measurements.CHAN3?.VAVG);
  checkRange(checks, '5V supply', supply, 4.75, 5.25);
  for (const source of ['CHAN1', 'CHAN2']) {
    const values = measurements[source] ?? {};
    checkRange(checks, `${source} frequency`, values.FREQUENCY, actualHz * 0.99, actualHz * 1.01);
    checkRange(checks, `${source} duty`, values.PDUTY, 45, 55);
    for (const item of ['PWIDTH', 'NWIDTH']) {
      checkRange(checks, `${source} ${item}`, values[item], 0.45 / actualHz, 0.55 / actualHz);
    }
    const summary = summaries[source];
    checks.push({ name: `${source} valid unclipped acquisition`,
      status: summary?.valid && summary.clipped === false ? 'pass' : 'inconclusive' });
    checkRange(checks, `${source} settled low`, summary?.statistics?.low_level_v, -0.3, 0.3);
    checkRange(checks, `${source} settled high`, summary?.statistics?.high_level_v,
      source === 'CHAN1' ? 3.0 : supply === null ? Infinity : supply - 0.5,
      source === 'CHAN1' ? 3.6 : supply === null ? -Infinity : supply + 0.3);
  }
  return { checks, automated_checks_passed: checks.every(check => check.status === 'pass'),
    qualified: false, manual_checks_pending: [
      'Inspect raw overshoot/undershoot and extra threshold crossings at each receiving pin.',
      'Repeat remaining AHCT244 paths, GAL truth-table and startup/power-cycle gates.',
    ] };
}

export async function readEdges(filename, threshold, hysteresis = 0.1) {
  const edges = [];
  let previous;
  let level;
  let crossing;
  let points = 0;
  let minimum = Infinity;
  let maximum = -Infinity;
  let start;
  let end;
  let step;
  for await (const row of createReadStream(filename).pipe(parse({ from_line: 2 }))) {
    const time = Number(row[0]);
    const voltage = numericMeasurement(row[1]);
    if (row.length !== 2 || !Number.isFinite(time) || voltage === null) {
      throw new Error(`Invalid sample in ${filename}`);
    }
    if (previous) {
      const interval = time - previous.time;
      if (!(interval > 0) || (step && Math.abs(interval - step) > step * 0.001)) {
        throw new Error(`Non-uniform timing in ${filename}`);
      }
      step ??= interval;
    }
    start ??= time;
    end = time;
    ++points;
    minimum = Math.min(minimum, voltage);
    maximum = Math.max(maximum, voltage);
    if (level === undefined) {
      if (voltage <= threshold - hysteresis) level = 0;
      if (voltage >= threshold + hysteresis) level = 1;
    } else if (previous) {
      const rising = level === 0;
      if ((rising && previous.voltage < threshold && voltage >= threshold) ||
          (!rising && previous.voltage > threshold && voltage <= threshold)) {
        crossing = previous.time + (time - previous.time) *
          (threshold - previous.voltage) / (voltage - previous.voltage);
      }
      if ((rising && voltage >= threshold + hysteresis) ||
          (!rising && voltage <= threshold - hysteresis)) {
        edges.push({ time: crossing ?? time, rising });
        level = rising ? 1 : 0;
        crossing = undefined;
      }
    }
    previous = { time, voltage };
  }
  if (points < 2) throw new Error(`Too few samples in ${filename}`);
  return { edges, points, start, end, sample_interval_s: step, minimum_v: minimum, maximum_v: maximum };
}

export function evaluateTrap(traces, actualHz, limits) {
  for (const key of ['minimum_high_s', 'minimum_low_s', 'maximum_last_edge_latency_s', 'maximum_pause_s']) {
    if (!(limits[key] > 0) || !Number.isFinite(limits[key])) throw new Error(`Supply a positive ${key}`);
  }
  if (!(actualHz > 0)) throw new Error('A configured actual frequency is required');
  const checks = [];
  const clock = traces.CHAN3;
  const sample = clock.sample_interval_s;
  const halfPeriod = 0.5 / actualHz;
  for (const [source, trace] of Object.entries(traces)) {
    if (trace.points !== clock.points || Math.abs(trace.start - clock.start) > sample * 0.01 ||
        Math.abs(trace.sample_interval_s - sample) > sample * 0.001) {
      throw new Error(`Unaligned capture: ${source}`);
    }
  }
  if (sample > Math.min(limits.minimum_high_s, limits.minimum_low_s) / 4) {
    return { checks, qualified: false, automated_checks_passed: false,
      status: 'inconclusive', reason: 'Insufficient samples across the minimum permitted clock pulse' };
  }
  const requests = [];
  let falling;
  for (const edge of traces.CHAN1.edges) {
    if (!edge.rising) falling = edge.time;
    else if (falling !== undefined) {
      requests.push({ start: falling, end: edge.time });
      falling = undefined;
    }
  }
  const gaps = [];
  const widths = { high: [], low: [] };
  for (let index = 1; index < clock.edges.length; ++index) {
    const before = clock.edges[index - 1];
    const after = clock.edges[index];
    const duration = after.time - before.time;
    if (duration > halfPeriod * 1.5) gaps.push({ start: before.time, end: after.time, duration });
    else widths[before.rising ? 'high' : 'low'].push(duration);
  }
  for (const level of ['high', 'low']) {
    checkRange(checks, `minimum unpaused clock ${level}`, widths[level].length ? widths[level].reduce((minimum, value) => Math.min(minimum, value), Infinity) : null,
      limits[`minimum_${level}_s`], Infinity);
  }
  checks.push({ name: 'complete I/O requests present', status: requests.length ? 'pass' : 'inconclusive', count: requests.length });
  const events = requests.map(request => {
    const pauses = gaps.filter(gap => gap.start >= request.start - halfPeriod && gap.end <= request.end + sample);
    const waitLow = traces.CHAN2.edges.find(edge => !edge.rising && edge.time >= request.start - sample && edge.time < request.end);
    const waitHigh = traces.CHAN2.edges.find(edge => edge.rising && waitLow && edge.time > waitLow.time && edge.time <= request.end + sample);
    const enable = traces.CHAN4.edges.find(edge => edge.rising && edge.time >= request.start && edge.time < request.end);
    const eventChecks = [];
    eventChecks.push({ name: 'exactly one intentional pause', status: pauses.length === 1 ? 'pass' : 'inconclusive' });
    eventChecks.push({ name: 'WAIT assertion and release captured', status: waitLow && waitHigh ? 'pass' : 'inconclusive' });
    eventChecks.push({ name: 'WAIT released after data enabled', status: waitHigh && enable
      ? waitHigh.time + sample >= enable.time ? 'pass' : 'fail' : 'inconclusive' });
    if (enable) {
      const earlyDisable = traces.CHAN4.edges.some(edge => !edge.rising && edge.time > enable.time && edge.time < request.end - sample);
      eventChecks.push({ name: 'data enabled until IORQ releases', status: earlyDisable ? 'fail' : 'pass' });
    }
    if (waitHigh) {
      const reasserted = traces.CHAN2.edges.some(edge => !edge.rising && edge.time > waitHigh.time && edge.time < request.end - sample);
      eventChecks.push({ name: 'no early WAIT reassertion', status: reasserted ? 'fail' : 'pass' });
    }
    if (pauses.length === 1) {
      checkRange(eventChecks, 'last clock edge after IORQ assertion', Math.max(0, pauses[0].start - request.start),
        0, limits.maximum_last_edge_latency_s);
      checkRange(eventChecks, 'extended clock interval', pauses[0].duration, halfPeriod * 1.5, limits.maximum_pause_s);
    }
    checks.push(...eventChecks);
    return { ...request, pauses, checks: eventChecks };
  });
  const unexpectedGaps = gaps.filter(gap => !requests.some(request =>
    gap.start >= request.start - halfPeriod && gap.end <= request.end + sample));
  checks.push({ name: 'all clock gaps explained by complete I/O requests',
    status: unexpectedGaps.length ? 'inconclusive' : 'pass', count: unexpectedGaps.length });
  return { checks, events, qualified: false,
    automated_checks_passed: checks.every(check => check.status === 'pass'),
    intentional_pauses_excluded_from_period_jitter: true,
    manual_checks_pending: [
      'Last-edge latency is not the instant PWM stopped; bound that uncertainty using a clock half-period.',
      'Verify WAIT setup/hold at the Z80 sampling edge and data setup/hold in separate captures.',
      'Repeat CH4 on RD#, WR#, DATA_DIR and both OE# nodes; prove no bus contention.',
      'Retain DSLogic Group A-D captures and one-hour functional workload results.',
    ] };
}