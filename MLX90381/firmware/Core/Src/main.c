/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "mlx90381.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MLX_DEBUG  false   /* Set to true for verbose debug messages */
#define NB_BYTE_WRITE  6   /* Number of bytes to write to register (default) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
/* MLX90381 driver handle */
MLX90381_HandleTypeDef hmlx;

/* Memory buffers */
uint16_t regMemoryWrite[MLX90381_REG_MEMORY_SIZE] = {0};
uint16_t regMemoryRead[MLX90381_REG_MEMORY_SIZE] = {0};
bool regMemoryHasRead = false;

uint16_t mtpMemoryWrite[MLX90381_MTP_MEMORY_SIZE] = {0};
uint16_t mtpMemoryRead[MLX90381_MTP_MEMORY_SIZE] = {0};
bool mtpMemoryHasRead = false;

/* I2C status */
uint8_t i2cStatus;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
static char UART_GetChar(void);
static void PrintStartupBanner(void);
static void HandleProgramRegister(void);
static void HandleCheckRegister(void);
static void HandleProgramMTP(void);
static void HandleReadMTP(void);
static void HandleProgramMemlock(void);
static void HandleLoadRegMemory(void);
static void HandleLoadMTPMemory(void);
static void HandleMeasureMode(void);
static void MeasureOutputs(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  Get single character from UART (blocking)
 * @retval Received character
 */
static char UART_GetChar(void)
{
    char c;
    HAL_UART_Receive(&hcom_uart[COM1], (uint8_t*)&c, 1, HAL_MAX_DELAY);
    return c;
}

/**
 * @brief  Print startup banner (when MLX_DEBUG is enabled)
 */
static void PrintStartupBanner(void)
{
    printf("==================================================================\n");
    printf("filename: MLX90381_PTC_HW_I2C/main.c\n");
    printf("I2C Master for MLX90381AA.\n\n");

    printf("Description:\n");
    printf("- STM32 HAL implementation for MLX90381AA I2C communication.\n");
    printf("- Ported from Mbed OS to vanilla STM32 HAL.\n");
    printf("- Implements the activation sequence for MLX90381.\n\n");

    printf("NOTE: You can not use a development board that has\n");
    printf("I2C pull-up resistors assembled on the PCB.\n");
    printf("The MLX90381 has internal pull-up resistors on the output\n");
    printf("pins that are activated when I2C communication is entered.\n\n");

    printf("Instructions:\n");
    printf("S: Program register.\n");
    printf("C: Check register data.\n");
    printf("P: Program MTP.\n");
    printf("R: Read MTP.\n");
    printf("L: Program MEMLOCK.\n");
    printf("Note: MEMLOCK is a permanent LOCK of the MTP.\n");
    printf("W: Enter new data to program register address by address.\n");
    printf("E: Enter new data to program MTP address by address.\n");
    printf("I: Identify firmware.\n");
    printf("A: Print last acknowledge status.\n\n");

    printf("version history:\n");
    printf("2021/06/11 Release v1.0: Initial Mbed version\n");
    printf("2025/01/09 Release v2.0: STM32 HAL port\n");
    printf("==================================================================\n\n");
}

/**
 * @brief  Handle 'S' command - Program register
 */
static void HandleProgramRegister(void)
{
    if (!regMemoryHasRead) {
        printf("Register memory has not been read. Please read register memory first.\n");
        return;
    }

    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
    i2cStatus = MLX90381_PTCEntry(&hmlx);

    if (i2cStatus == MLX90381_PTC_FAIL) {
        MLX90381_PrintAcknowledge(i2cStatus);
        return;
    }

    if (hmlx.i2c_mode != 2 && hmlx.i2c_mode != 4) {
        /* Enter calibration mode */
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Program customer register */
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_PROGRAM_CUSTOMER_REG);
    i2cStatus = MLX90381_WriteRegister(&hmlx, MLX90381_REG_CUSTOMER, regMemoryWrite, NB_BYTE_WRITE);

    if (NB_BYTE_WRITE != 6) {
        /* Enter application mode keeping calibration mode valid */
        i2cStatus = MLX90381_EnterCalAppMode(&hmlx);
    }

    MLX90381_ReleaseOutputs(&hmlx);

    if (i2cStatus == MLX90381_OK) {
        printf(" 0 : success ACK\n");
    } else {
        MLX90381_PrintAcknowledge(i2cStatus);
    }
}

/**
 * @brief  Handle 'C' command - Check register data
 */
static void HandleCheckRegister(void)
{
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
    i2cStatus = MLX90381_PTCEntry(&hmlx);

    if (i2cStatus == MLX90381_PTC_FAIL) {
        MLX90381_PrintAcknowledge(i2cStatus);
        return;
    }

    if (hmlx.i2c_mode != 2 && hmlx.i2c_mode != 4) {
        /* Enter calibration mode */
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Read Customer Register */
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_READ_CUSTOMER_REG);
    memset(regMemoryRead, 0, sizeof(regMemoryRead));
    i2cStatus = MLX90381_ReadMemory(&hmlx, MLX90381_REG_CUSTOMER, regMemoryRead, 8);

    for (uint8_t i = 0; i < 8; i++) {
        printf("%X ", 0x20 + i * 2);
        printf("%X ", regMemoryRead[i]);
    }
    printf("\n");

    /* Enter application mode keeping calibration mode valid */
    i2cStatus = MLX90381_EnterCalAppMode(&hmlx);

    MLX90381_ReleaseOutputs(&hmlx);

    if (i2cStatus != MLX90381_OK) {
        MLX90381_PrintAcknowledge(i2cStatus);
    }
    regMemoryHasRead = true;
}

/**
 * @brief  Handle 'P' command - Program MTP
 */
static void HandleProgramMTP(void)
{
    if (!mtpMemoryHasRead) {
        printf("MTP memory has not been read. Please read MTP memory first.\n");
        return;
    }

    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
    i2cStatus = MLX90381_PTCEntry(&hmlx);

    if (i2cStatus == MLX90381_PTC_FAIL) {
        MLX90381_PrintAcknowledge(i2cStatus);
        return;
    }

    if (hmlx.i2c_mode != 2 && hmlx.i2c_mode != 4) {
        /* Enter calibration mode */
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Enter MTP write mode */
    i2cStatus = MLX90381_EnterMTPWriteMode(&hmlx);

    /* Program Customer MTP */
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_PROGRAM_CUSTOMER_MTP);
    i2cStatus = MLX90381_WriteMTP(&hmlx, 0x0000, mtpMemoryWrite, 8);

    /* After MTP writes, sensor may have timed out I2C (20-30ms timeout).
       Re-activate I2C and re-enter calibration mode before continuing. */
    if (MLX_DEBUG) printf("Re-activating I2C after MTP writes...\n");
    i2cStatus = MLX90381_PTCEntry(&hmlx);
    if (i2cStatus == MLX90381_PTC_SUCCESS) {
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Deactivate MTP and reset write mode */
    i2cStatus = MLX90381_ResetMTPMode(&hmlx);

    /* Enter application mode with MTP configuration after write */
    i2cStatus = MLX90381_EnterNormalAppMode(&hmlx);

    MLX90381_ReleaseOutputs(&hmlx);

    MLX90381_PrintAcknowledge(i2cStatus);
}

/**
 * @brief  Handle 'R' command - Read MTP
 */
static void HandleReadMTP(void)
{
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
    i2cStatus = MLX90381_PTCEntry(&hmlx);

    if (i2cStatus == MLX90381_PTC_FAIL) {
        MLX90381_PrintAcknowledge(i2cStatus);
        return;
    }

    if (hmlx.i2c_mode != 2 && hmlx.i2c_mode != 4) {
        /* Enter calibration mode */
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Enter MTP read mode */
    i2cStatus = MLX90381_EnterMTPReadMode(&hmlx);

    /* Read first 8 words (0x00-0x0E) */
    memset(regMemoryRead, 0, sizeof(regMemoryRead));
    i2cStatus = MLX90381_ReadMemory(&hmlx, 0x0000, regMemoryRead, 8);

    for (uint8_t i = 0; i < 8; i++) {
        printf("%X ", i * 2);
        printf("%X ", regMemoryRead[i]);
    }

    /* Read next 8 words (0x10-0x1E) */
    memset(regMemoryRead, 0, sizeof(regMemoryRead));
    i2cStatus = MLX90381_ReadMemory(&hmlx, 0x0010, regMemoryRead, 8);

    for (uint8_t i = 0; i < 8; i++) {
        printf("%X ", 0x10 + i * 2);
        printf("%X ", regMemoryRead[i]);
    }
    printf("\n");

    /* Deactivate MTP and reset write mode */
    i2cStatus = MLX90381_ResetMTPMode(&hmlx);

    /* Enter application mode with MTP configuration */
    i2cStatus = MLX90381_EnterNormalAppMode(&hmlx);

    MLX90381_ReleaseOutputs(&hmlx);

    if (i2cStatus != MLX90381_OK) {
        MLX90381_PrintAcknowledge(i2cStatus);
    }
    mtpMemoryHasRead = true;
}

/**
 * @brief  Handle 'L' command - Program MEMLOCK (permanent!)
 */
static void HandleProgramMemlock(void)
{
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
    i2cStatus = MLX90381_PTCEntry(&hmlx);

    if (i2cStatus == MLX90381_PTC_FAIL) {
        MLX90381_PrintAcknowledge(i2cStatus);
        return;
    }

    if (hmlx.i2c_mode != 2 && hmlx.i2c_mode != 4) {
        /* Enter calibration mode */
        i2cStatus = MLX90381_EnterCalibrationMode(&hmlx);
    }

    /* Enter MTP write mode */
    i2cStatus = MLX90381_EnterMTPWriteMode(&hmlx);

    /* Program MTP Lock */
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_PROGRAM_MTP_LOCK);
    uint16_t lockValue = MLX90381_MTP_LOCK;
    i2cStatus = MLX90381_WriteMTP(&hmlx, MLX90381_REG_MTP_LOCK, &lockValue, 1);

    /* Deactivate MTP and reset write mode */
    i2cStatus = MLX90381_ResetMTPMode(&hmlx);

    /* Enter application mode with MTP configuration after MEMLOCK */
    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ENTER_APP_AFTER_LOCK);
    i2cStatus = MLX90381_EnterNormalAppMode(&hmlx);

    MLX90381_ReleaseOutputs(&hmlx);

    MLX90381_PrintAcknowledge(i2cStatus);
}

/**
 * @brief  Handle 'W' command - Load Register MemoryWrite values manually
 */
static void HandleLoadRegMemory(void)
{
    char incomingChar[10];
    int incomingInt;
    char incomingByte;
    int a;

    for (uint8_t i = 0; i < 8; i++) {
        a = 0;

        printf("ADD %02X\n", 0x20 + i * 2);

        /* Read up to 4 characters for the value */
        do {
            incomingChar[a] = UART_GetChar();
            a++;
            if (a > 4) break;
        } while (incomingChar[a-1] != '\n' && incomingChar[a-1] != '\r');

        incomingChar[a] = '\0';
        sscanf(incomingChar, "%d", &incomingInt);
        regMemoryWrite[i] = (uint16_t)incomingInt;
        printf("%d\n", regMemoryWrite[i]);

        /* Wait for confirmation: 'y' = next, 'n' = retry, 'a' = abort */
        do {
            incomingByte = UART_GetChar();
        } while (incomingByte != 'n' && incomingByte != 'y' && incomingByte != 'a');

        if (incomingByte == 'n') {
            i--;  /* Retry this address */
        }
        if (incomingByte == 'a') {
            break;  /* Abort */
        }
    }
}

/**
 * @brief  Handle 'E' command - Load MTP MemoryWrite values manually
 */
static void HandleLoadMTPMemory(void)
{
    char incomingChar[10];
    int incomingInt;
    char incomingByte;
    int a;

    for (uint8_t i = 0; i < 8; i++) {
        a = 0;

        printf("ADD %02X\n", 0x00 + i * 2);

        /* Read up to 4 characters for the value */
        do {
            incomingChar[a] = UART_GetChar();
            a++;
            if (a > 4) break;
        } while (incomingChar[a-1] != '\n' && incomingChar[a-1] != '\r');

        incomingChar[a] = '\0';
        sscanf(incomingChar, "%d", &incomingInt);
        mtpMemoryWrite[i] = (uint16_t)incomingInt;
        printf("%d\n", mtpMemoryWrite[i]);

        /* Wait for confirmation: 'y' = next, 'n' = retry, 'a' = abort */
        do {
            incomingByte = UART_GetChar();
        } while (incomingByte != 'n' && incomingByte != 'y' && incomingByte != 'a');

        if (incomingByte == 'n') {
            i--;  /* Retry this address */
        }
        if (incomingByte == 'a') {
            break;  /* Abort */
        }
    }
}

/**
 * @brief  Handle 'M' command - Measure outputs in application mode
 * @note   Reads analog outputs OUT1 (A4/PC1) and OUT2 (A5/PC0)
 */
static void HandleMeasureMode(void)
{
    /* Check if already in normal application mode */
    if (hmlx.mtp_mode != 64 || hmlx.i2c_mode != 1) {  /* MTPresetMode=64, NormApplMode=1 */
        /* Activate I2C interface */
        if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
        i2cStatus = MLX90381_PTCEntry(&hmlx);

        if (i2cStatus == MLX90381_PTC_SUCCESS) {
            /* Deactivate MTP and reset write mode */
            i2cStatus = MLX90381_ResetMTPMode(&hmlx);

            /* Enter application mode with MTP configuration */
            i2cStatus = MLX90381_EnterNormalAppMode(&hmlx);
        }

        MLX90381_ReleaseOutputs(&hmlx);

        /* Wait 2000-3000µs for output drivers to switch on */
        HAL_Delay(3);
    }

    if (i2cStatus == MLX90381_OK || i2cStatus == MLX90381_PTC_SUCCESS) {
        /* Measure analog outputs */
        MeasureOutputs();
    } else {
        MLX90381_PrintAcknowledge(i2cStatus);
    }
}

/**
 * @brief  Read and print analog outputs OUT1 and OUT2
 * @note   Requires ADC1 configured with IN6 (PC0/A5) and IN7 (PC1/A4)
 */
static void MeasureOutputs(void)
{
#ifdef HAL_ADC_MODULE_ENABLED
    extern ADC_HandleTypeDef hadc1;
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t out1_value, out2_value;

    if (MLX_DEBUG) MLX90381_PrintMessage(MSG_MEASURE_OUTPUT);

    /* Wait for output drivers to stabilize */
    HAL_Delay(3);

    /* Configure and read OUT1 (PC1 = ADC1_IN7 = A4) */
    sConfig.Channel = ADC_CHANNEL_7;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    out1_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Configure and read OUT2 (PC0 = ADC1_IN6 = A5) */
    sConfig.Channel = ADC_CHANNEL_6;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    out2_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    printf(" OUT1 %lu OUT2 %lu\n", out1_value, out2_value);
#else
    printf("ADC not configured. Enable ADC1 with IN6 (PC0) and IN7 (PC1) in CubeMX.\n");
#endif
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize led */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* Initialize MLX90381 driver */
  MLX90381_Init(&hmlx, &hi2c1, MLX_DEBUG);

  /* Print startup banner if debug enabled */
  if (MLX_DEBUG) {
      PrintStartupBanner();
  }

  /* Initial PTC entry and mode setup */
  if (MLX_DEBUG) MLX90381_PrintMessage(MSG_ACTIVATE_I2C);
  i2cStatus = MLX90381_PTCEntry(&hmlx);

  if (i2cStatus == MLX90381_PTC_SUCCESS) {
      /* Deactivate MTP and reset write mode */
      i2cStatus = MLX90381_ResetMTPMode(&hmlx);

      /* Enter application mode with MTP configuration */
      i2cStatus = MLX90381_EnterNormalAppMode(&hmlx);
  }

  MLX90381_ReleaseOutputs(&hmlx);

  if (i2cStatus != MLX90381_OK) {
      MLX90381_PrintAcknowledge(i2cStatus);
  }
  
  char incomingByte;

  while (1)
  {
    /* Get instruction */
    incomingByte = UART_GetChar();

    switch (incomingByte) {
        case 'A':
            /* Print last acknowledge status */
            MLX90381_PrintAcknowledge(i2cStatus);
            break;

        case 'S':
            /* Program register */
            HandleProgramRegister();
            break;

        case 'C':
            /* Check register data */
            HandleCheckRegister();
            break;

        case 'P':
            /* Program MTP */
            HandleProgramMTP();
            break;

        case 'R':
            /* Read MTP */
            HandleReadMTP();
            break;

        case 'L':
            /* Program MEMLOCK (permanent!) */
            HandleProgramMemlock();
            break;

        case 'W':
            /* Load MemoryWrite values manually */
            HandleLoadRegMemory();
            break;

        case 'E':
            /* Load MTP MemoryWrite values manually */
            HandleLoadMTPMemory();
            break;

        case 'M':
            /* Enter application mode with MTP configuration */
            HandleMeasureMode();
            break;

        case 'I':
            /* Identify firmware */
            printf("90381\n");
            break;

        default:
            /* Ignore unknown commands */
            break;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0xD071C1FF;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
