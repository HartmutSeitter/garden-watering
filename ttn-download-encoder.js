// this downlink encoder is working
// in NodeRed is send the following json objact to the enc node

/*
temp_f_port = msg.payload.uplink_message.f_port;
temp_f_cnt = msg.payload.uplink_message.f_cnt;
msg.payload={}

msg.payload = {
   
  "downlinks": [{
    "f_port": 1,
    "decoded_payload": {"data": {"color":"green"}},
    //"frm_payload": "vu8=",
    "priority": "NORMAL"
  }]
}

return msg;

*/

// on ttn side the encoder converts the json object send via mqtt 
// in a download msg of 01 which is the index of green in color

function encodeDownlink(input) {
  var colors = ["red", "green", "yellow"];
  
  return {
    bytes: [colors.indexOf(input.data.color)],
    fPort: 1,
    warnings: [],
    errors: []
  };
}



//------------------------------------------------------------
//temp_f_port = msg.payload.uplink_message.f_port;
//temp_f_cnt = msg.payload.uplink_message.f_cnt;
msg.payload={}
var bytes = [];
bytes[0] = 1;
bytes[1] = 02;
bytes[2] = 02;
bytes[3] = 02;
bytes[4] = 02;
bytes[5] = 04;
bytes[6] = 04;

msg.payload = {
  
  "downlinks": [{
    "f_port": 1,
    "frm_payload": Buffer.from(bytes).toString('base64'),
    //"frm_payload": "01181920182122",
    "priority": "HIGH"
  }]
}

return msg;


//
und kein encoder - das funktioniert nicht immer




260B991F