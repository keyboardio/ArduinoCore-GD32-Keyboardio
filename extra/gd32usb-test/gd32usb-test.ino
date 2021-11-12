#include "KeyboardioHID.h"

void setup()
{
				//Serial.begin(115200);
				Serial.println("-- Blink demo --");

				pinMode(LED2, OUTPUT);

				Keyboard.begin();
				Mouse.begin();
}

int f = 0;

void loop()
{
				Serial.println("On");
				digitalWrite(LED2, HIGH);
				delay(500);

				Mouse.move(10, 0);
				f++;
				Mouse.sendReport();
				f++;
				Mouse.releaseAll();
				f++;
				Mouse.sendReport();
				f++;

				// Delay long enough to see the mouse move.
				delay(500);

				Keyboard.press(HID_KEYBOARD_A_AND_A);
				f++;
				Keyboard.sendReport();
				f++;
				Keyboard.releaseAll();
				f++;
				Keyboard.sendReport();
				f++;

				Serial.println("Off");
				digitalWrite(LED2, LOW);
}
