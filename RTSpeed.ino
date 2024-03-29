float temp = 0;

char msg[8];
char data[6];

void setup()
{
    Serial.begin(115200, SERIAL_8N1);
    Serial.setTimeout(10);
}

void loop()
{
    temp = sin(micros()*1e-6 * 2*PI*10);

    strcpy(msg, "");
    dtostrf(temp, 6, 3, msg);

    Serial.println(msg);
    Serial.flush();
}