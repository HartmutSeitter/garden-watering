// TTN Payload Formatter — garden-watering-node
// Paste "decodeUplink" into TTN Console → Application → Payload formatters → Uplink
// Paste "encodeDownlink" into TTN Console → Application → Payload formatters → Downlink

// ─── UPLINK DECODER ──────────────────────────────────────────────────────────
// Payload units: flow values are in centilitres (cL), 1 L = 100 cL
// PULSES_PER_LITER = 595 (calibrated), conversion done in firmware before sending
function decodeUplink(input) {
  var b = input.bytes;
  var event = b[0];

  if (event === 1) {
    // Flow data (every 15s while water flows)
    // [1][0][timeMs 4B][flowCl 2B][rawPulses 2B]
    var timeMs    = ((b[2]<<24)|(b[3]<<16)|(b[4]<<8)|b[5]) >>> 0;
    var flowCl    = (b[6]<<8)|b[7];
    var rawPulses = b.length >= 10 ? (b[8]<<8)|b[9] : null;
    var out = {
      event:           1,
      timeinterval_ms: timeMs,
      flow_cl:         flowCl,
      flow_liter:      parseFloat((flowCl / 100).toFixed(2))
    };
    if (rawPulses !== null) {
      out.raw_pulses = rawPulses;
      // Calibration hint: PULSES_PER_LITER = raw_pulses / actual_volume_liter
      // (measure a known volume, divide raw_pulses by that volume)
    }
    return { data: out };
  }

  if (event === 2) {
    // On/off schedule + counter limit + max pulses per interval (every 10 min)
    // [2][0][onH][onM][onS][offH][offM][offS][cntrCl 2B][maxPI 2B]
    var cntrCl = (b[8]<<8)|b[9];
    var maxPI  = (b[10]<<8)|b[11];
    return { data: {
      event: 2,
      on_time:  pad(b[2]) + ':' + pad(b[3]) + ':' + pad(b[4]),
      off_time: pad(b[5]) + ':' + pad(b[6]) + ':' + pad(b[7]),
      on_hour: b[2], on_min: b[3], on_sec: b[4],
      off_hour: b[5], off_min: b[6], off_sec: b[7],
      cntr_cl:    cntrCl,
      cntr_liter: parseFloat((cntrCl / 100).toFixed(2)),
      max_pulses_per_interval: maxPI
    }};
  }

  if (event === 3) {
    // Current date/time from RTC (every 10 min)
    // [3][0][year 2B][month][day][hour][min][sec]
    return { data: {
      event: 3,
      year:  (b[2]<<8)|b[3],
      month: b[4], day: b[5],
      hour:  b[6], min: b[7], sec: b[8]
    }};
  }

  if (event === 4) {
    // Watering session started
    return { data: { event: 4, status: 'watering_started' }};
  }

  if (event === 5) {
    // Flow alarm — excessive flow rate detected, valve closed
    // [5][0][pulses 2B]  (raw pulses in 5s interval)
    var pulses = (b[2]<<8)|b[3];
    return { data: {
      event:        5,
      status:       'flow_alarm',
      alarm_pulses: pulses
    }};
  }

  if (event === 7) {
    // Watering session ended (normal close)
    // [7][0][totalFlowCl 2B][totalMs 4B]
    var totalCl = (b[2]<<8)|b[3];
    var totalMs = ((b[4]<<24)|(b[5]<<16)|(b[6]<<8)|b[7]) >>> 0;
    return { data: {
      event:        7,
      status:       'watering_ended',
      total_cl:     totalCl,
      total_liter:  parseFloat((totalCl / 100).toFixed(2)),
      duration_ms:  totalMs,
      duration_min: parseFloat((totalMs / 60000).toFixed(1))
    }};
  }

  return { errors: ['unknown event: ' + event] };
}

function pad(n) { return n < 10 ? '0' + n : '' + n; }

// ─── NODE-RED "decode payload" FUNCTION NODE ─────────────────────────────────
// Paste into the "decode payload" function node in Node-RED
/*
try {
    var raw = Buffer.from(msg.payload.uplink_message.frm_payload, 'base64');
    var t = raw[0];
    msg.eventType = t;
    msg.devEui = msg.topic.split('/')[3];

    if (t === 1) {
        msg.flowData = {
            timeinterval: ((raw[2]<<24)|(raw[3]<<16)|(raw[4]<<8)|raw[5]) >>> 0,
            flowCl: (raw[6]<<8)|raw[7]
        };
    } else if (t === 2) {
        msg.schedule = {
            onH:raw[2], onM:raw[3], onS:raw[4],
            offH:raw[5], offM:raw[6], offS:raw[7],
            cntrCl:(raw[8]<<8)|raw[9],
            maxPI:(raw[10]<<8)|raw[11]
        };
    } else if (t === 4) {
        msg.wateringStart = { status: 'watering_started' };
    } else if (t === 5) {
        msg.alarmPulses = (raw[2]<<8)|raw[3];
    } else if (t === 7) {
        msg.wateringEnd = {
            totalFlowCl: (raw[2]<<8)|raw[3],
            totalTimeMs: ((raw[4]<<24)|(raw[5]<<16)|(raw[6]<<8)|raw[7]) >>> 0
        };
    }
    return msg;
} catch(e) {
    node.error('decode error: ' + e.message, msg);
    return null;
}
*/


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

// ─── DOWNLINK DECODER ────────────────────────────────────────────────────────
// TTN calls this to display sent downlinks in the console log
function decodeDownlink(input) {
  var b = input.bytes;
  var event = b[0];

  if (event === 1) {
    return { data: {
      event: 1,
      on_time:  pad(b[1]) + ':' + pad(b[2]) + ':' + pad(b[3]),
      off_time: pad(b[4]) + ':' + pad(b[5]) + ':' + pad(b[6])
    }};
  }

  if (event === 3) {
    return { data: {
      event: 3,
      year:  (b[1] << 8) | b[2],
      month: b[3], day: b[4],
      hour:  b[5], min: b[6], sec: b[7]
    }};
  }

  if (event === 4) {
    var cntr = (b[1] << 8) | b[2];
    return { data: {
      event:            4,
      cntr_value_cl:    cntr,
      cntr_value_liter: parseFloat((cntr / 100).toFixed(2))
    }};
  }

  if (event === 6) {
    return { data: {
      event:      6,
      max_pulses: (b[1] << 8) | b[2]
    }};
  }

  return { errors: ['unknown event: ' + event] };
}

// ─── DOWNLINK ENCODER ────────────────────────────────────────────────────────
function encodeDownlink(input) {
  var d = input.data;

  if (d.event === 1) {
    // Set on/off schedule
    return {
      bytes: [1, d.on_hour, d.on_min, d.on_sec, d.off_hour, d.off_min, d.off_sec],
      fPort: 1, warnings: [], errors: []
    };
  }

  if (d.event === 3) {
    // Set RTC date/time
    // { "event": 3, "year": 2026, "month": 5, "day": 5, "hour": 20, "min": 0, "sec": 0 }
    var yr = d.year;
    return {
      bytes: [3, (yr >> 8) & 0xFF, yr & 0xFF, d.month, d.day, d.hour, d.min, d.sec],
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
