#ifndef ENCODER_CONTROL_H
#define ENCODER_CONTROL_H

void encoderInit();
void encoderLoop();               // Call in main loop (handles tick())
int encoderGetPosition();
void encoderSetPosition(int pos); // Set position
bool encoderPositionChanged();    // True if position changed since last check
bool encoderButtonPressed();      // True on button press (debounced)

#endif
