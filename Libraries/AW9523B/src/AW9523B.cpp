#include "AW9523B.h"
#include "HardwareSerial.h"

AW9523B::AW9523B() {
#ifdef AW9523B_DEBUG
    DEBUG_PRINTER.begin(115200);
#endif
    DEBUG_PRINTLN("Call Contructor");
}
#if defined(ESP32) || defined(ESP8266)
AW9523B::AW9523B(int8_t sda, int8_t scl, int8_t ad0, int8_t ad1) 
: sda(sda), scl(scl), ad0(ad0), ad1(ad1) {
#ifdef AW9523B_DEBUG
    DEBUG_PRINTER.begin(115200);
#endif
    DEBUG_PRINTLN("Call Contructor");
}
#endif
AW9523B::~AW9523B() {

}

void AW9523B::begin() {
    // Initial wait after Power Up
    // delay(100);
    // Initialize I2C
    if(sda != -1 && scl != -1) {
        Wire.begin(sda, scl); 
    }
    else {
        Wire.begin(); 
    }
    // Reset AW9523
    //reset(); 
}

void AW9523B::reset() {
#ifdef DEBUG
    DEBUG_PRINTF("Resetting...\n"); 
#endif
    writeByte(AW9523B_ADDR_RESET, 0); 
#ifdef DEBUG
    DEBUG_PRINTF("Reset Finished\n"); 
#endif
}

void AW9523B::setAddress(uint8_t _ad0, uint8_t _ad1) {
    ad0 = _ad0; 
    ad1 = _ad1; 
} 

uint8_t AW9523B::readPort(uint8_t port) {
    DEBUG_PRINTF("Read Port %d\n", port); 
    if(port == 0) {
        return readByte(AW9523B_ADDR_INPUT0); 
    }
    else if(port == 1) {
        return readByte(AW9523B_ADDR_INPUT1); 
    }
    else {
        DEBUG_PRINTF("Read Port is Invalid"); 
        return 0; 
    }
} 

void AW9523B::writePort(uint8_t port, uint8_t data) {
    DEBUG_PRINTF("Write Port %d / Data 0x%02x\n", port, data); 
    if(port == 0) {
        writeByte(AW9523B_ADDR_OUTPUT0, data); 
    }
    else if(port == 1) {
        writeByte(AW9523B_ADDR_OUTPUT1, data); 
    }
    else {
        DEBUG_PRINTF("Write Port is Invalid"); 
    }
} 

void AW9523B::setConfig(uint8_t port, uint8_t mode) {
    if(port == 0) {
        writeByte(AW9523B_ADDR_CONFIG0, mode); 
    }
    else if(port == 1) {
        writeByte(AW9523B_ADDR_CONFIG1, mode); 
    }
}

void AW9523B::setInterrupt(uint8_t port, uint8_t flag) {
    if(port == 0) {
        writeByte(AW9523B_ADDR_INT0, flag); 
    }
    else if(port == 1) {
        writeByte(AW9523B_ADDR_INT1, flag); 
    }
}

uint8_t AW9523B::readID() {
    return readByte(AW9523B_ADDR_ID); 
}

void AW9523B::setGlobalControl(GPIO_DRV_MODE_Enum mode, uint8_t range) {
    uint8_t data = ((mode & 0x01) << 4) | (range & 0x03); 
    writeByte(AW9523B_ADDR_GCR, data); 
}
    
void AW9523B::setLedMode(uint8_t port, uint8_t flag) {
    if(port == 0) {
        writeByte(AW9523B_ADDR_LEDMODE0, ~flag); 
    }
    else if(port == 1) {
        writeByte(AW9523B_ADDR_LEDMODE1, ~flag);
    }
}

void AW9523B::setGpioMode(uint8_t port, uint8_t flag) {
    if(port == 0) {
        writeByte(AW9523B_ADDR_LEDMODE0, flag); 
    }
    else if(port == 1) {
        writeByte(AW9523B_ADDR_LEDMODE1, flag); 
    }
}

void AW9523B::setDimmer(uint8_t port, uint8_t subport, uint8_t dim) {
    if(port == 0) {
        writeByte(AW9523B_ADDR_DIM_BASE + 4 + subport, dim); 
    }
    else if(port == 1) {
        if(subport < 4) {
            writeByte(AW9523B_ADDR_DIM_BASE + subport, dim); 
        }
        else {
            writeByte(AW9523B_ADDR_DIM_BASE + 8 + subport, dim); 
        }
    }
} 

// Private Function
uint8_t AW9523B::readByte(uint8_t addr) {
    uint8_t rdData; 
    uint8_t rdDataCount; 
    do {
        Wire.beginTransmission(AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0); 
        Wire.write(addr); 
        Wire.endTransmission(false); // Restart
        //delay(10); 
        rdDataCount = Wire.requestFrom(AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0, 1); 
    } while(rdDataCount == 0); 
    while(Wire.available()) {
        rdData = Wire.read(); 
    }
    return rdData; 

}
void AW9523B::writeByte(uint8_t addr, uint8_t data) {
    Wire.beginTransmission(AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0); 
    Wire.write(addr); 
    Wire.write(data); 
    Wire.endTransmission(); 
}