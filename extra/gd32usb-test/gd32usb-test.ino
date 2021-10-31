#include "KeyboardioHID.h"

void setup()
{
				Serial.begin(115200);
				Serial.println("-- Blink demo --");

				pinMode(LED2, OUTPUT);

				BootKeyboard.begin();
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

				Serial.println("Off");
				digitalWrite(LED2, LOW);
				delay(500);
}
