
#pragma once

#if defined(SERIAL_BAUD)

#define  DEBUGGING  1
#define  DEBUG(...)     Serial.print(__VA_ARGS__)
#define  DEBUGLN(...)   Serial.println(__VA_ARGS__)
#else
#define  DEBUG(...)
#define  DEBUGLN(...)
#endif
