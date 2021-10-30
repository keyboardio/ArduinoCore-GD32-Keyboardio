#include "TestPlug.h"

void setup()
{
				Serial.begin(115200);
				Serial.println("-- Blink demo --");

				pinMode(LED2, OUTPUT);

				Serial.println(tp.foo());
}

void loop()
{
				Serial.println("On");
				digitalWrite(LED2, HIGH);
				delay(500);

				Serial.println("Off");
				digitalWrite(LED2, LOW);
				delay(500);
}
