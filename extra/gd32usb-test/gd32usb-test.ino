#include "KeyboardioHID.h"

void setup()
{
    Serial.begin(115200);
    Serial.println("-- Blink demo --");

    pinMode(LED2, OUTPUT);

    BootKeyboard.begin();
    Mouse.begin();
}

void loop()
{
    Serial.println("On");
    digitalWrite(LED2, HIGH);
    delay(500);

    BootKeyboard.press(HID_KEYBOARD_A_AND_A);
    BootKeyboard.sendReport();
    BootKeyboard.releaseAll();
    BootKeyboard.sendReport();

    Mouse.move(10, 0);
    Mouse.sendReport();
    Mouse.releaseAll();
    Mouse.sendReport();

    Serial.println("Off");
    digitalWrite(LED2, LOW);

    // Delay long enough to see the mouse move.
    delay(500);
}
