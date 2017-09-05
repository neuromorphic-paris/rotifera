#include <Servo.h>

/// Input represents a pwm signal read on a interrupt pin.
struct Input {
    const byte pin;
    void (*interruptCallback)();
    volatile unsigned int value;
    volatile boolean hasNewMeasure;
    volatile boolean isCounting;
    volatile unsigned long start;
};

/// Output wraps a servo object with its pin.
struct Output {
    const byte pin;
    const int zero;
    Servo servo;
};

/// interruptCallback is called whenever a pwm signal changes.
void interruptCallback(Input* input) {
    if (digitalRead(input->pin) == HIGH) {
        input->start = micros();
        input->isCounting = true;
    } else if (input->isCounting) {
        input->value = micros() - input->start;
        input->isCounting = false;
        input->hasNewMeasure = true;
    }
}

/// Declare the inputs and the associated callbacks.
Input inputs[] = {
    {2, directionInterruptCallback}, // direction
    {3, throttleInterruptCallback}, // throttle
};
void directionInterruptCallback() {
    interruptCallback(&inputs[0]);
}
void throttleInterruptCallback() {
    interruptCallback(&inputs[1]);
}

/// Declare the outputs.
Output outputs[] = {
    {22, 1500}, // direction
    {24, 1552}, // throttle
    {18, 1500}, // pan
    {20, 1500}, // tilt
};

/// Declare the read state machine.
byte previousBytes[] = {0, 0};
byte expectedByteId = 0;
unsigned long lastRead = 0;

void setup() {
    Serial.begin(230400);
    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(Input); ++index) {
        inputs[index].value = 0;
        inputs[index].hasNewMeasure = false;
        inputs[index].isCounting = false;
        inputs[index].start = 0;
        attachInterrupt(digitalPinToInterrupt(inputs[index].pin), inputs[index].interruptCallback, CHANGE);
    }
    for (unsigned int index = 0; index < sizeof(outputs) / sizeof(Output); ++index) {
        outputs[index].servo.attach(outputs[index].pin);
        outputs[index].servo.writeMicroseconds(outputs[index].zero);
    }
}

void loop() {
    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(Input); ++index) {
        if (inputs[index].hasNewMeasure) {
            inputs[index].hasNewMeasure = false;
            const unsigned int value = inputs[index].value;

            //             | LSB  | bit 1 | bit 2 | bit 3 | bit 4 | bit 5 | bit 6 | MSB
            // ------------|------|-------|-------|-------|-------|-------|-------|-------
            // First byte  | 0    | 0     | i[0]  | i[1]  | i[2]  | i[3]  | i[4]  | i[5]
            // Second byte | 1    | 0     | v[0]  | v[1]  | v[2]  | v[3]  | v[4]  | v[5]
            // Third byte  | 0    | 1     | v[6]  | v[7]  | v[8]  | v[9]  | v[10] | v[11]
            byte bytes[3] = {
                (byte)(0 | (index << 2)),
                (byte)(1 | (value << 2)),
                (byte)(2 | ((value >> 4) & 0xfc)),
            };
            Serial.write(bytes, 3);
        }
    }

    if (Serial.available()) {
        byte newByte = Serial.read();
        if ((newByte & 0x3) != expectedByteId) {
            expectedByteId = 0;
        } else if (expectedByteId < 2) {
            previousBytes[expectedByteId] = newByte;
            ++expectedByteId;
        } else {
            expectedByteId = 0;
            const unsigned int index = (previousBytes[0] >> 2);
            if (index < sizeof(outputs) / sizeof(Output)) {
                outputs[index].servo.writeMicroseconds((unsigned int)(previousBytes[1] >> 2) | ((unsigned int)(newByte & 0xfc) << 4));
            }
        }
        lastRead = millis();
    } else if (millis() - lastRead > 1000) {
        outputs[1].servo.writeMicroseconds(outputs[1].zero);
        lastRead = millis();
    }
}
