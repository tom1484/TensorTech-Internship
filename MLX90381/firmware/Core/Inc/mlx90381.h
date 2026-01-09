/**
 * @file    mlx90381.h
 * @brief   MLX90381 Magnetic Position Sensor Driver
 * @note    Ported from Mbed OS to STM32 HAL
 */

#ifndef MLX90381_H
#define MLX90381_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* MLX90381 I2C Address (7-bit: 0x32, shifted for HAL: 0x64) */
#define MLX90381_I2C_ADDR           (0x32 << 1)

/* Sensor Mode Constants - Command Register (0x0044) */
#define MLX90381_MODE_NORM_APPL     0x944C  /* Normal application mode with MTP config */
#define MLX90381_MODE_CALIBRATION   0x544E  /* Calibration mode */
#define MLX90381_MODE_CAL_APPL      0x744C  /* Calibration application mode */

/* MTP Control Constants - MTP Control Register (0x0046) */
#define MLX90381_MTP_WRITE          0x0077  /* MTP write mode */
#define MLX90381_MTP_READ           0x0007  /* MTP read mode */
#define MLX90381_MTP_RESET          0x0006  /* MTP reset/deactivate */

/* MTP Lock Value */
#define MLX90381_MTP_LOCK           0x0003  /* Write to 0x000C to lock MTP */

/* Register Addresses */
#define MLX90381_REG_CMD            0x0044  /* Command register */
#define MLX90381_REG_MTP_CTRL       0x0046  /* MTP control register */
#define MLX90381_REG_CUSTOMER       0x0020  /* Customer register start */
#define MLX90381_REG_MTP_LOCK       0x000C  /* MTP lock register */

/* Status codes */
#define MLX90381_OK                 0x00
#define MLX90381_NACK               0x01
#define MLX90381_PTC_FAIL           0xFE
#define MLX90381_PTC_SUCCESS        0xFF

/* Number of customer registers/MTP words */
#define MLX90381_REG_MEMORY_SIZE        8
#define MLX90381_MTP_MEMORY_SIZE        16
#define MLX90381_REG_NB_WORDS           6

/* Default timing configuration */
#define MLX90381_DEFAULT_BAUDRATE       25000   /* I2C baud rate in Hz */
#define MLX90381_DEFAULT_DELAY_INST     5       /* Instruction overhead in µs */
#define MLX90381_DEFAULT_I2C_TIMEOUT    100     /* I2C timeout in ms */

/* Driver context */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
    uint32_t baudrate;      /* I2C baud rate in Hz */
    uint32_t delay_inst;    /* Instruction execution overhead in µs */
    uint32_t ptc_delay_us;  /* Calculated PTC half-clock delay in µs */
    uint16_t i2c_timeout;   /* I2C operation timeout in ms */
    uint8_t i2c_mode;       /* Current I2C sensor mode */
    uint8_t mtp_mode;       /* Current MTP mode */
    bool debug;             /* Enable debug messages */
} MLX90381_HandleTypeDef;

/* Function prototypes */
void MLX90381_Init(MLX90381_HandleTypeDef *hmlx, I2C_HandleTypeDef *hi2c, bool debug);
uint8_t MLX90381_PTCEntry(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_ReadMemory(MLX90381_HandleTypeDef *hmlx, uint16_t addr, uint16_t *data, uint8_t count);
uint8_t MLX90381_WriteRegister(MLX90381_HandleTypeDef *hmlx, uint16_t addr, uint16_t *data, uint8_t count);
uint8_t MLX90381_WriteMTP(MLX90381_HandleTypeDef *hmlx, uint16_t addr, uint16_t *data, uint8_t count);
void MLX90381_ReleaseOutputs(MLX90381_HandleTypeDef *hmlx);

/* High-level operations */
uint8_t MLX90381_EnterCalibrationMode(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_EnterNormalAppMode(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_EnterCalAppMode(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_EnterMTPWriteMode(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_EnterMTPReadMode(MLX90381_HandleTypeDef *hmlx);
uint8_t MLX90381_ResetMTPMode(MLX90381_HandleTypeDef *hmlx);

/* Debug helpers */
void MLX90381_PrintAcknowledge(uint8_t status);
void MLX90381_PrintMessage(uint8_t message);

/* Message IDs for PrintMessage */
#define MSG_ACTIVATE_I2C            1
#define MSG_ENTER_CALIB             2
#define MSG_ENTER_MTP_WRITE         3
#define MSG_DEACTIVATE_MTP          4
#define MSG_ENTER_NORM_APP          5
#define MSG_PROGRAM_CUSTOMER_REG    6
#define MSG_ENTER_CAL_APP           7
#define MSG_PROGRAM_CUSTOMER_MTP    8
#define MSG_READ_CUSTOMER_REG       9
#define MSG_MEASURE_OUTPUT          10
#define MSG_PROGRAM_MTP_LOCK        11
#define MSG_ENTER_APP_AFTER_LOCK    12

#endif /* MLX90381_H */

