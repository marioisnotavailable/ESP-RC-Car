#include <Arduino.h>
#include <SimpleFOC.h>

#define PIN_INHA   18
#define PIN_INLA    8
#define PIN_INHB    3
#define PIN_INLB    9
#define PIN_INHC   10
#define PIN_INLC   11
#define PIN_DRV_EN 16
#define PIN_DRV_FAULT 39

BLDCMotor motor = BLDCMotor(7);
BLDCDriver6PWM driver = BLDCDriver6PWM(PIN_INHA, PIN_INLA, PIN_INHB, PIN_INLB, PIN_INHC, PIN_INLC);

void setup() {
    Serial.begin(115200);

    pinMode(PIN_DRV_EN, OUTPUT);
    digitalWrite(PIN_DRV_EN, HIGH);
    delay(50);

    if (digitalRead(PIN_DRV_FAULT) == LOW) {
        Serial.println("DRV8323 fault at startup");
    }

    driver.voltage_power_supply = 12;
    driver.pwm_frequency = 20000;
    if (!driver.init()) {
        Serial.println("Driver init failed");
        while (1);
    }

    motor.linkDriver(&driver);
    motor.controller = MotionControlType::velocity_openloop;
    motor.voltage_limit = 4;
    motor.velocity_limit = 20;
    motor.init();
    motor.initFOC();

    Serial.println("SimpleFOC ready — spinning at 5 rad/s");
}

void loop() {
    motor.loopFOC();
    motor.move(5); // rad/s forward
}
