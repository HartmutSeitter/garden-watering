#ifndef _FLOWSENSOR_H_
#define _FLOWSENSOR_H_

void setup_flowsensor(void);
unsigned long read_flowsensor(void);
void flowsensor_enable(void);   // attach interrupt (call when valve opens)
void flowsensor_disable(void);  // detach interrupt (call when valve closes)

#endif
