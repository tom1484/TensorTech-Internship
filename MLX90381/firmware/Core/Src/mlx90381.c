/**
 * @file    mlx90381.c
 * @brief   MLX90381 Magnetic Position Sensor Driver Implementation
 * @note    Ported from Mbed OS to STM32 HAL
 */

 #include "mlx90381.h"
 #include <stdio.h>
 
 /* Private function prototypes */
 static void delay_us(uint32_t us);
 static void MLX90381_ReinitI2C(MLX90381_HandleTypeDef *hmlx);
 
 /**
  * @brief  Microsecond delay using DWT cycle counter
  * @param  us: Delay in microseconds
  */
 static void delay_us(uint32_t us)
 {
     uint32_t startTick = DWT->CYCCNT;
     uint32_t delayTicks = us * (SystemCoreClock / 1000000);
     while ((DWT->CYCCNT - startTick) < delayTicks);
 }
 
 /**
  * @brief  Initialize DWT for microsecond timing
  */
 static void DWT_Init(void)
 {
     CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
     DWT->CYCCNT = 0;
     DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
 }
 
 /**
  * @brief  Re-initialize I2C peripheral after GPIO bit-banging
  * @param  hmlx: Pointer to MLX90381 handle
  */
 static void MLX90381_ReinitI2C(MLX90381_HandleTypeDef *hmlx)
 {
     GPIO_InitTypeDef GPIO_InitStruct = {0};
     
     /* Configure SCL pin for I2C alternate function */
     GPIO_InitStruct.Pin = hmlx->scl_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
     GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
     HAL_GPIO_Init(hmlx->scl_port, &GPIO_InitStruct);
     
     /* Configure SDA pin for I2C alternate function */
     GPIO_InitStruct.Pin = hmlx->sda_pin;
     GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
     HAL_GPIO_Init(hmlx->sda_port, &GPIO_InitStruct);
     
     /* Re-initialize I2C */
     HAL_I2C_Init(hmlx->hi2c);
 }
 
 /**
  * @brief  Initialize MLX90381 driver
  * @param  hmlx: Pointer to MLX90381 handle
  * @param  hi2c: Pointer to I2C handle
  * @param  debug: Enable debug messages
  */
 void MLX90381_Init(MLX90381_HandleTypeDef *hmlx, I2C_HandleTypeDef *hi2c, bool debug)
 {
     hmlx->hi2c = hi2c;
     hmlx->scl_port = GPIOB;
     hmlx->scl_pin = GPIO_PIN_8;   /* PB8 = I2C1_SCL */
     hmlx->sda_port = GPIOB;
     hmlx->sda_pin = GPIO_PIN_9;   /* PB9 = I2C1_SDA */
     hmlx->i2c_mode = 0;
     hmlx->mtp_mode = 0;
     hmlx->debug = debug;
     
     /* Timing configuration */
     hmlx->baudrate = MLX90381_DEFAULT_BAUDRATE;
     hmlx->delay_inst = MLX90381_DEFAULT_DELAY_INST;
     hmlx->i2c_timeout = MLX90381_DEFAULT_I2C_TIMEOUT;
     
     /* Calculate PTC half-clock delay: (1000000 / baudrate / 2) - instruction_overhead */
     /* For 25kHz: 1000000 / 25000 / 2 - 5 = 20 - 5 = 15 µs */
     uint32_t half_period = 1000000 / hmlx->baudrate / 2;
     if (half_period > hmlx->delay_inst) {
         hmlx->ptc_delay_us = half_period - hmlx->delay_inst;
     } else {
         hmlx->ptc_delay_us = 5;  /* Minimum delay fallback */
     }
     
     /* Initialize DWT for microsecond delays */
     DWT_Init();
 }
 
 /**
  * @brief  PTC Entry - Wake up sensor I2C interface via bit-banging
  * @note   This temporarily reconfigures I2C pins as GPIO
  * @param  hmlx: Pointer to MLX90381 handle
  * @retval MLX90381_PTC_SUCCESS or MLX90381_PTC_FAIL
  */
 uint8_t MLX90381_PTCEntry(MLX90381_HandleTypeDef *hmlx)
 {
     GPIO_InitTypeDef GPIO_InitStruct = {0};
     uint8_t i;
     uint8_t readACK = 0;
     uint8_t readStart;
     
     /* De-init I2C to use pins as GPIO */
     HAL_I2C_DeInit(hmlx->hi2c);
        
     /* Configure SDA as PUSH-PULL output LOW to create overcurrent > 500µA */
     /* Doc 4.1: "SDA pin needs a low-side driver (or push-pull) that can create 
        an overcurrent OC > 500µA on the output driver to switch the drivers off" */
     GPIO_InitStruct.Pin = hmlx->sda_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  /* Push-pull for overcurrent! */
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
     HAL_GPIO_Init(hmlx->sda_port, &GPIO_InitStruct);
     HAL_GPIO_WritePin(hmlx->sda_port, hmlx->sda_pin, GPIO_PIN_RESET);
     
     /* Configure SCL as input with pull-down */
     GPIO_InitStruct.Pin = hmlx->scl_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
     GPIO_InitStruct.Pull = GPIO_PULLDOWN;
     HAL_GPIO_Init(hmlx->scl_port, &GPIO_InitStruct);
     
     delay_us(hmlx->ptc_delay_us * 2);
     
     /* Wait for SCL to go low (sensor output drivers off) */
     /* 100kOhm pull-downs on sensor pins should pull line low when output drivers are OFF */
     for (i = 0; i < 25; i++) {
         readStart = HAL_GPIO_ReadPin(hmlx->scl_port, hmlx->scl_pin);
         if (readStart == GPIO_PIN_RESET)
             break;
         delay_us(hmlx->ptc_delay_us);
     }
     
     /* The drivers should switch off within 250 microseconds */
     if (i >= 25) {
         MLX90381_ReinitI2C(hmlx);
         return MLX90381_PTC_FAIL;
     }
     
     /* Configure SCL as PUSH-PULL output for clocking */
     /* Doc 4.1: "SCL pin needs to have a push-pull driver to set the 8 clock pulses" */
     GPIO_InitStruct.Pin = hmlx->scl_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  /* Push-pull for clock pulses! */
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     HAL_GPIO_Init(hmlx->scl_port, &GPIO_InitStruct);
     HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_RESET);
     
     /* Configure SDA as input (floating) */
     /* 100kOhm pull-downs on sensor pins should keep the SDA line low */
     GPIO_InitStruct.Pin = hmlx->sda_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     HAL_GPIO_Init(hmlx->sda_port, &GPIO_InitStruct);
     
     delay_us(hmlx->ptc_delay_us);
     
     /* Send 8 clock pulses */
     for (i = 0; i < 8; i++) {
         HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_SET);
         delay_us(hmlx->ptc_delay_us);
         /* Dummy read to keep clock timing */
         (void)HAL_GPIO_ReadPin(hmlx->sda_port, hmlx->sda_pin);
         HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_RESET);
         delay_us(hmlx->ptc_delay_us);
         /* Dummy read to keep clock timing */
         (void)HAL_GPIO_ReadPin(hmlx->sda_port, hmlx->sda_pin);
     }
     
     /* Check !ACK of the sensor */
     /* Internal 10kOhm pull-up on sensor pins will be set if 8 clocks are received well */
     HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_SET);
     delay_us(hmlx->ptc_delay_us);
     
     for (i = 0; i < 10; i++) {
         readACK = HAL_GPIO_ReadPin(hmlx->sda_port, hmlx->sda_pin);
         if (readACK == GPIO_PIN_SET)
             break;
         /* The internal 10kOhm pull-up should switch on within 50 microseconds */
         delay_us(5);
     }
     
     if (i >= 10) {
         /* ACK failed */
         delay_us(hmlx->ptc_delay_us * 10);
         MLX90381_ReinitI2C(hmlx);
         return MLX90381_PTC_FAIL;
     }
     
     HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_RESET);
     delay_us(hmlx->ptc_delay_us);
     
     /* Send STOP condition: SCL high, then SDA low-to-high */
     HAL_GPIO_WritePin(hmlx->scl_port, hmlx->scl_pin, GPIO_PIN_SET);
     delay_us(hmlx->ptc_delay_us);
     
     /* SDA push-pull high for STOP */
     GPIO_InitStruct.Pin = hmlx->sda_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  /* Push-pull for STOP */
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     HAL_GPIO_Init(hmlx->sda_port, &GPIO_InitStruct);
     HAL_GPIO_WritePin(hmlx->sda_port, hmlx->sda_pin, GPIO_PIN_SET);
     delay_us(hmlx->ptc_delay_us);
     
     /* Re-initialize I2C peripheral */
     MLX90381_ReinitI2C(hmlx);
     
     return MLX90381_PTC_SUCCESS;
 }
 
 /**
  * @brief  Read memory/registers from sensor
  * @param  hmlx: Pointer to MLX90381 handle
  * @param  addr: Start address to read from (16-bit)
  * @param  data: Pointer to buffer for read data (16-bit words)
  * @param  count: Number of 16-bit words to read
  * @retval HAL status
  * @note   Uses repeated start between address write and data read phases
  *         Protocol: S-Addr[W]-A-AddrMSB-A-AddrLSB-A-SR-Addr[R]-A-Data...-NA-P
  */
 uint8_t MLX90381_ReadMemory(MLX90381_HandleTypeDef *hmlx, uint16_t addr, 
                             uint16_t *data, uint8_t count)
 {
     uint8_t rxBuf[16];
     HAL_StatusTypeDef status;
     
     /* Use HAL_I2C_Mem_Read which generates proper repeated start */
     /* MemAddSize = 2 bytes (16-bit address) */
     status = HAL_I2C_Mem_Read(hmlx->hi2c, MLX90381_I2C_ADDR, addr, 
                               I2C_MEMADD_SIZE_16BIT, rxBuf, count * 2, 
                               hmlx->i2c_timeout);
     if (status != HAL_OK) {
         return status;
     }
     
     /* Convert to 16-bit words (big-endian) */
     for (uint8_t i = 0; i < count; i++) {
         data[i] = ((uint16_t)rxBuf[i * 2] << 8) | rxBuf[i * 2 + 1];
     }
     
     return MLX90381_OK;
 }
 
 /**
  * @brief  Write to sensor registers (addresses >= 0x20)
  * @param  hmlx: Pointer to MLX90381 handle
  * @param  addr: Start address to write to (16-bit)
  * @param  data: Pointer to data buffer (16-bit words)
  * @param  count: Number of 16-bit words to write
  * @retval HAL status
  * @note   Protocol: S-Addr[W]-A-AddrMSB-A-AddrLSB-A-DataMSB-A-DataLSB-A-...-P
  */
 uint8_t MLX90381_WriteRegister(MLX90381_HandleTypeDef *hmlx, uint16_t addr,
                                uint16_t *data, uint8_t count)
 {
     uint8_t txBuf[16];  /* Data buffer (max 8 words = 16 bytes) */
     HAL_StatusTypeDef status;
     
     /* Write only to registers (addr >= 0x20) */
     if (addr < 0x20) {
         return MLX90381_NACK;
     }
     /* Limit count to maximum number of words for registers */
     if (count > MLX90381_REG_NB_WORDS) {
         count = MLX90381_REG_NB_WORDS;
     }
     /* Pack data (big-endian: MSB first for each 16-bit word) */
     for (uint8_t i = 0; i < count; i++) {
         txBuf[i * 2] = (data[i] >> 8) & 0xFF;       /* MSB */
         txBuf[i * 2 + 1] = data[i] & 0xFF;          /* LSB */
     }
     
     /* Use HAL_I2C_Mem_Write with 16-bit address */
     status = HAL_I2C_Mem_Write(hmlx->hi2c, MLX90381_I2C_ADDR, addr,
                                I2C_MEMADD_SIZE_16BIT, txBuf, count * 2,
                                hmlx->i2c_timeout);
     
     if (status != HAL_OK && hmlx->debug) {
         printf("Reg Write Error at addr 0x%04X\n", addr);
         printf("  HAL Status: %d, I2C Error: 0x%08lX\n", status, hmlx->hi2c->ErrorCode);
     }
     
     return status;
 }
 
 /**
  * @brief  Write to MTP memory (addresses < 0x20, with 11ms delay per word)
  * @param  hmlx: Pointer to MLX90381 handle
  * @param  addr: Start address to write to (16-bit)
  * @param  data: Pointer to data buffer (16-bit words)
  * @param  count: Number of 16-bit words to write
  * @retval HAL status
  * @note   Each word requires 10ms+ delay for MTP erase/write cycle
  *         Protocol: S-Addr[W]-A-AddrMSB-A-AddrLSB-A-DataMSB-A-DataLSB-A-P (per word)
  */
 uint8_t MLX90381_WriteMTP(MLX90381_HandleTypeDef *hmlx, uint16_t addr,
                           uint16_t *data, uint8_t count)
 {
     uint8_t txBuf[2];
     HAL_StatusTypeDef status = HAL_OK;
     
     /* Write only to MTP (addr < 0x20) */
     if (addr >= 0x20) {
         return MLX90381_NACK;
     }
     
     /* Write one word at a time with delay */
     for (uint8_t i = 0; i < count; i++) {
         uint16_t currentAddr = addr + (i * 2);
         
         /* Pack data (big-endian: MSB first) */
         txBuf[0] = (data[i] >> 8) & 0xFF;       /* MSB */
         txBuf[1] = data[i] & 0xFF;              /* LSB */
         
         /* Use HAL_I2C_Mem_Write with 16-bit address */
         status = HAL_I2C_Mem_Write(hmlx->hi2c, MLX90381_I2C_ADDR, currentAddr,
                                    I2C_MEMADD_SIZE_16BIT, txBuf, 2,
                                    hmlx->i2c_timeout);
         if (status != HAL_OK) {
             /* Debug: Print error details */
             if (hmlx->debug) {
                 printf("MTP Write Error at addr 0x%04X, word %d\n", currentAddr, i);
                 printf("  HAL Status: %d (1=ERROR, 2=BUSY, 3=TIMEOUT)\n", status);
                 printf("  I2C Error Code: 0x%08lX\n", hmlx->hi2c->ErrorCode);
                 /* Error codes: HAL_I2C_ERROR_BERR=0x01, AF(NACK)=0x04, OVR=0x08, 
                    DMA=0x10, TIMEOUT=0x20, SIZE=0x40 */
                 if (hmlx->hi2c->ErrorCode & 0x04) {
                     printf("  -> NACK received (sensor not ready or MTP locked?)\n");
                 }
             }
             return status;
         }
         
         if (hmlx->debug) {
             printf("MTP[0x%02X] = 0x%04X OK\n", currentAddr, data[i]);
         }
         
         /* Minimum 10 milliseconds required to erase and write MTP cells */
         HAL_Delay(11);
     }
     
     return status;
 }
 
 /**
  * @brief  Release I2C outputs (switch to input mode)
  * @param  hmlx: Pointer to MLX90381 handle
  */
 void MLX90381_ReleaseOutputs(MLX90381_HandleTypeDef *hmlx)
 {
     GPIO_InitTypeDef GPIO_InitStruct = {0};
     
     /* Configure SCL as input */
     GPIO_InitStruct.Pin = hmlx->scl_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     HAL_GPIO_Init(hmlx->scl_port, &GPIO_InitStruct);
     
     /* Configure SDA as input */
     GPIO_InitStruct.Pin = hmlx->sda_pin;
     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
     GPIO_InitStruct.Pull = GPIO_NOPULL;
     HAL_GPIO_Init(hmlx->sda_port, &GPIO_InitStruct);
 }
 
 /* High-level mode operations */
 
 uint8_t MLX90381_EnterCalibrationMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MODE_CALIBRATION;
     if (hmlx->debug) MLX90381_PrintMessage(MSG_ENTER_CALIB);
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_CMD, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->i2c_mode = 2;  /* CalibrationMode */
     }
     return status;
 }
 
 uint8_t MLX90381_EnterNormalAppMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MODE_NORM_APPL;
     if (hmlx->debug) MLX90381_PrintMessage(MSG_ENTER_NORM_APP);
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_CMD, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->i2c_mode = 1;  /* NormApplMode */
     }
     return status;
 }
 
 uint8_t MLX90381_EnterCalAppMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MODE_CAL_APPL;
     if (hmlx->debug) MLX90381_PrintMessage(MSG_ENTER_CAL_APP);
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_CMD, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->i2c_mode = 4;  /* CalApplMode */
     }
     return status;
 }
 
 uint8_t MLX90381_EnterMTPWriteMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MTP_WRITE;
     if (hmlx->debug) MLX90381_PrintMessage(MSG_ENTER_MTP_WRITE);
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_MTP_CTRL, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->mtp_mode = 16;  /* MTPwriteMode */
     }
     return status;
 }
 
 uint8_t MLX90381_EnterMTPReadMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MTP_READ;
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_MTP_CTRL, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->mtp_mode = 32;  /* MTPreadMode */
     }
     return status;
 }
 
 uint8_t MLX90381_ResetMTPMode(MLX90381_HandleTypeDef *hmlx)
 {
     uint16_t cmd = MLX90381_MTP_RESET;
     if (hmlx->debug) MLX90381_PrintMessage(MSG_DEACTIVATE_MTP);
     uint8_t status = MLX90381_WriteRegister(hmlx, MLX90381_REG_MTP_CTRL, &cmd, 1);
     if (status == MLX90381_OK) {
         hmlx->mtp_mode = 64;  /* MTPresetMode */
     }
     return status;
 }
 
 /* Debug helper functions */
 
 /**
  * @brief  Print I2C acknowledge status
  * @param  status: I2C status code
  */
 void MLX90381_PrintAcknowledge(uint8_t status)
 {
     switch (status) {
         case MLX90381_PTC_FAIL:
             printf(" FE: I2C interface activation fail\n");
             break;
         case MLX90381_PTC_SUCCESS:
             printf(" FF: I2C interface activation success\n");
             break;
         case MLX90381_OK:
             printf(" 0 : success ACK\n");
             break;
         case MLX90381_NACK:
             printf(" 1 : Received NACK\n");
             break;
         default:
             printf(" ? : Unknown\n");
             break;
     }
 }
 
 /**
  * @brief  Print debug message
  * @param  message: Message ID
  */
 void MLX90381_PrintMessage(uint8_t message)
 {
     switch (message) {
         case MSG_ACTIVATE_I2C:
             printf("Activate I2C interface.\n");
             break;
         case MSG_ENTER_CALIB:
             printf("Enter calibration mode.\n");
             break;
         case MSG_ENTER_MTP_WRITE:
             printf("Enter MTP write mode.\n");
             break;
         case MSG_DEACTIVATE_MTP:
             printf("Deactivate MTP and reset write mode.\n");
             break;
         case MSG_ENTER_NORM_APP:
             printf("Enter application mode with MTP configuration.\n");
             break;
         case MSG_PROGRAM_CUSTOMER_REG:
             printf("Program customer register.\n");
             break;
         case MSG_ENTER_CAL_APP:
             printf("Enter application mode keeping calibration mode valid with register configuration.\n");
             break;
         case MSG_PROGRAM_CUSTOMER_MTP:
             printf("Program Customer MTP.\n");
             break;
         case MSG_READ_CUSTOMER_REG:
             printf("Read Customer Register.\n");
             break;
         case MSG_MEASURE_OUTPUT:
             printf("Perform measurements output.\n");
             break;
         case MSG_PROGRAM_MTP_LOCK:
             printf("Program MTP Lock.\n");
             break;
         case MSG_ENTER_APP_AFTER_LOCK:
             printf("Enter application mode with MTP configuration after MEMLOCK.\n");
             break;
         default:
             printf("Message unknown\n");
             break;
     }
 } 