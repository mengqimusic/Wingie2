#include "AW9523B.h"
#include <stdio.h>

#define I2C_SDA 22
#define I2C_SCL 23
#define RESET_N 2
AW9523B aw9523b_0(I2C_SDA, I2C_SCL, 1, 1); 
AW9523B aw9523b_1(I2C_SDA, I2C_SCL, 1, 0); 

void setup() {
    Serial.begin(115200); 

    // Start AW9523B
    aw9523b_0.begin(); 
    aw9523b_1.begin(); 
    Serial.println("[AW9523B] begin."); 

    pinMode(RESET_N, OUTPUT); 
    digitalWrite(RESET_N, 0); 
    delay(100); 
    digitalWrite(RESET_N, 1); 
    delay(100); 
    Serial.print("[AW9523B_0] ID: 0x"); 
    Serial.println(aw9523b_0.readID(), HEX); 
    Serial.print("[aw9523b_1] ID: 0x"); 
    Serial.println(aw9523b_1.readID(), HEX); 

    aw9523b_0.setLedMode(0, 0xFF); 
    aw9523b_0.setLedMode(1, 0xFF); 
    aw9523b_1.setLedMode(0, 0x0F); 
    aw9523b_1.setLedMode(1, 0x0F); 
}

void loop() {
    uint8_t dim = 0; 
    uint8_t pol = 0; 
    // Red LED
    dim = 1; 
    pol = 0; 
    while(dim != 0) {
        if(dim == 255) {
            pol = 1; 
        }
        if(pol == 0) {
            dim++; 
        }
        else {
            dim--; 
        }
        aw9523b_0.setDimmer(0, 0, dim); 
        aw9523b_0.setDimmer(0, 3, dim); 
        aw9523b_0.setDimmer(0, 6, dim); 
        aw9523b_0.setDimmer(1, 1, dim); 
        aw9523b_0.setDimmer(1, 5, dim); 
        aw9523b_1.setDimmer(0, 2, dim); 
        aw9523b_1.setDimmer(1, 0, dim); 
        aw9523b_1.setDimmer(1, 3, dim); 
        delay(10); 
    }
    // Green LED
    dim = 1; 
    pol = 0; 
    while(dim != 0) {
        if(dim == 255) {
            pol = 1; 
        }
        if(pol == 0) {
            dim++; 
        }
        else {
            dim--; 
        }
        aw9523b_0.setDimmer(0, 1, dim); 
        aw9523b_0.setDimmer(0, 4, dim); 
        aw9523b_0.setDimmer(0, 7, dim); 
        aw9523b_0.setDimmer(1, 2, dim); 
        aw9523b_0.setDimmer(1, 6, dim); 
        aw9523b_1.setDimmer(0, 0, dim); 
        aw9523b_1.setDimmer(0, 3, dim); 
        aw9523b_1.setDimmer(1, 1, dim); 
        delay(10); 
    }
    // Blue LED
    dim = 1; 
    pol = 0; 
    while(dim != 0) {
        if(dim == 255) {
            pol = 1; 
        }
        if(pol == 0) {
            dim++; 
        }
        else {
            dim--; 
        }
        aw9523b_0.setDimmer(0, 2, dim); 
        aw9523b_0.setDimmer(0, 5, dim); 
        aw9523b_0.setDimmer(1, 0, dim); 
        aw9523b_0.setDimmer(1, 3, dim); 
        aw9523b_0.setDimmer(1, 4, dim); 
        aw9523b_0.setDimmer(1, 7, dim); 
        aw9523b_1.setDimmer(0, 1, dim); 
        aw9523b_1.setDimmer(1, 2, dim); 
        delay(10); 
    }

}