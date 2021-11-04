#include "KeyboardioHID.h"

void setup()
{
				//Serial.begin(115200);
				Serial.println("-- Blink demo --");

				pinMode(LED2, OUTPUT);

				BootKeyboard.begin();
}

int f = 0;

void loop()
{
				Serial.println("On");
				digitalWrite(LED2, HIGH);
				delay(500);

				BootKeyboard.press(HID_KEYBOARD_A_AND_A);
				f++;
				BootKeyboard.sendReport();
				f++;
				BootKeyboard.releaseAll();
				f++;
				BootKeyboard.sendReport();
				f++;

				Serial.println("Off");
				digitalWrite(LED2, LOW);
				delay(500);
}
