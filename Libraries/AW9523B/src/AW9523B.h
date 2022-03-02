#ifndef AW9523B_H
#define AW9523B_H

#include <Wire.h>
#include <stdint.h>
#include <stdbool.h>

// Device Address
#define AW9523B_I2C_BASE_ADDRESS 0x58 // 7-bit Address
// Register Address
#define AW9523B_ADDR_INPUT0 0x00
#define AW9523B_ADDR_INPUT1 0x01
#define AW9523B_ADDR_OUTPUT0 0x02
#define AW9523B_ADDR_OUTPUT1 0x03
#define AW9523B_ADDR_CONFIG0 0x04
#define AW9523B_ADDR_CONFIG1 0x05
#define AW9523B_ADDR_INT0 0x06
#define AW9523B_ADDR_INT1 0x07
#define AW9523B_ADDR_ID 0x10
#define AW9523B_ADDR_GCR 0x11
typedef enum {
    DRV_MODE_OPEN_DRAIN = 0, 
    DRV_MODE_PUSH_PULL = 1, 
} GPIO_DRV_MODE_Enum; 
#define AW9523B_ADDR_LEDMODE0 0x12
#define AW9523B_ADDR_LEDMODE1 0x13
#define AW9523B_ADDR_DIM_BASE 0x20
#define AW9523B_ADDR_RESET 0x7F

// Uncomment to enable debug messages
//#define AW9523B_DEBUG

// Define where debug output will be printed
#define DEBUG_PRINTER Serial

// Setup debug printing macros
#ifdef AW9523B_DEBUG
#define DEBUG_PRINT(...) \
    { DEBUG_PRINTER.printf("[AW9523B(0x%02x)]: ", AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0); DEBUG_PRINTER.print(__VA_ARGS__); }
#define DEBUG_PRINTLN(...) \
    { DEBUG_PRINTER.printf("[AW9523B(0x%02x)]: ", AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0); DEBUG_PRINTER.println(__VA_ARGS__); }
#define DEBUG_PRINTF(...) \
    { DEBUG_PRINTER.printf("[AW9523B(0x%02x)]: ", AW9523B_I2C_BASE_ADDRESS + (ad1 << 1) + ad0); DEBUG_PRINTER.printf(__VA_ARGS__); }
#else
#define DEBUG_PRINT(...) \
    {}
#define DEBUG_PRINTLN(...) \
    {}
#define DEBUG_PRINTF(...) \
    {}
#endif

class AW9523B
{
public: 
    AW9523B(void); 
#if defined(ESP32) || defined(ESP8266)
    AW9523B(int8_t sda, int8_t scl, int8_t ad0, int8_t ad1); 
#endif
    virtual ~AW9523B(); 

    void begin(); 
    
    void reset(); 

    void setAddress(uint8_t _ad0, uint8_t _ad1); 

    uint8_t readPort(uint8_t port); 
    void writePort(uint8_t port, uint8_t data); 
    void setConfig(uint8_t port, uint8_t mode); 
    void setInterrupt(uint8_t port, uint8_t flag); 

    uint8_t readID(); 

    void setGlobalControl(GPIO_DRV_MODE_Enum mode, uint8_t range); 
    
    void setLedMode(uint8_t port, uint8_t flag); 
    void setGpioMode(uint8_t port, uint8_t flag); 
    void setDimmer(uint8_t port, uint8_t subport, uint8_t dim); 

private: 
    int8_t sda = -1; 
    int8_t scl = -1; 
    int8_t ad0 = -1; 
    int8_t ad1 = -1; 
    
    uint8_t readByte(uint8_t addr); 
    void writeByte(uint8_t addr, uint8_t data); 

}; 

#endif