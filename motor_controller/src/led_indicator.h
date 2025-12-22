#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

void ledInit();
void ledLoop();           // Call in main loop (handles blink timing + color updates)
void ledShowStopped();    // Set mode: blinking red
void ledShowRunning();    // Set mode: color gradient

#endif
