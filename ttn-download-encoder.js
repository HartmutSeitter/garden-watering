// TTN Payload Formatter — garden-watering-node
// Paste "decodeUplink" into TTN Console → Application → Payload formatters → Uplink
// Paste "encodeDownlink" into TTN Console → Application → Payload formatters → Downlink

// ─── UPLINK DECODER ──────────────────────────────────────────────────────────
function decodeUplink(input) {
  var b = input.bytes;
  var event = b[0];

  if (event === 1) {
    // Flow data (every 15s while water flows)
    var timeinterval = (b[2]<<24)|(b[3]<<16)|(b[4]<<8)|b[5];
    var flowDelta    = (b[6]<<8)|b[7];
    return { data: {
      event: 1,
      timeinterval_ms: timeinterval,
      flow_delta:      flowDelta,
      flow_liter:      parseFloat((flowDelta / 180.0).toFixed(3))
    }};
  }

  if (event === 2) {
    // On/off schedule + counter limit + max pulses per interval (every 10 min)
    return { data: {
      event: 2,
      on_time:    pad(b[2]) + ':' + pad(b[3]) + ':' + pad(b[4]),
      off_time:   pad(b[5]) + ':' + pad(b[6]) + ':' + pad(b[7]),
      on_hour: b[2], on_min: b[3], on_sec: b[4],
      off_hour: b[5], off_min: b[6], off_sec: b[7],
      cntr_value: (b[8]<<8)|b[9],
      max_pulses_per_interval: (b[10]<<8)|b[11]
    }};
  }

  if (event === 3) {
    // Current date/time from RTC (every 10 min)
    return { data: {
      event: 3,
      year:  (b[2]<<8)|b[3],
      month: b[4],
      day:   b[5],
      hour:  b[6],
      min:   b[7],
      sec:   b[8]
    }};
  }

  if (event === 4) {
    // Watering session started
    return { data: { event: 4, status: 'watering_started' }};
  }

  if (event === 5) {
    // Flow alarm — excessive flow rate detected, valve closed
    var pulses = (b[2]<<8)|b[3];
    return { data: {
      event:  5,
      status: 'flow_alarm',
      alarm_pulses:       pulses,
      alarm_liter_per_5s: parseFloat((pulses / 180.0).toFixed(2))
    }};
  }

  if (event === 7) {
    // Watering session ended (normal close)
    var totalFlow = (b[2]<<8)|b[3];
    var totalMs   = ((b[4]<<24)|(b[5]<<16)|(b[6]<<8)|b[7]) >>> 0;
    return { data: {
      event:        7,
      status:       'watering_ended',
      total_flow:   totalFlow,
      total_liter:  parseFloat((totalFlow / 180.0).toFixed(1)),
      duration_ms:  totalMs,
      duration_min: parseFloat((totalMs / 60000.0).toFixed(1))
    }};
  }

  return { errors: ['unknown event type: ' + event] };
}

function pad(n) { return n < 10 ? '0' + n : '' + n; }


// ─── DOWNLINK ENCODER ────────────────────────────────────────────────────────
// Send via Node-RED or TTN Console with decoded_payload:
//
// Set schedule:
//   { "event": 1, "on_hour": 20, "on_min": 0, "on_sec": 0,
//                 "off_hour": 20, "off_min": 5, "off_sec": 0 }
//
// Set counter limit (max total pulses per window):
//   { "event": 4, "cntr_value": 500 }
//
// Set max pulses per 5s interval (leak alarm threshold):
//   { "event": 6, "max_pulses": 80 }

function encodeDownlink(input) {
  var d = input.data;

  if (d.event === 1) {
    // Set on/off schedule
    return {
      bytes: [1, d.on_hour, d.on_min, d.on_sec, d.off_hour, d.off_min, d.off_sec],
      fPort: 1, warnings: [], errors: []
    };
  }

  if (d.event === 4) {
    // Set max total pulses (counter limit)
    var c = d.cntr_value;
    return {
      bytes: [4, (c >> 8) & 0xFF, c & 0xFF],
      fPort: 1, warnings: [], errors: []
    };
  }

  if (d.event === 6) {
    // Set max pulses per 5s interval (leak threshold)
    var m = d.max_pulses;
    return {
      bytes: [6, (m >> 8) & 0xFF, m & 0xFF],
      fPort: 1, warnings: [], errors: []
    };
  }

  return { errors: ['unknown event: ' + d.event] };
}
