/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
#include "CLCD.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Manual mode screen */
typedef enum
{
  EN_VOLTAGE = 0x00,
  EN_SWITCH_SIGNAL,
  EN_DEFAULT              /* Request to go back to voltage screen */
} SETTING_STATE;

/* App connection state */
typedef enum
{
  DIS_APP = 0x00,
  CON_APP
} SETTING_APP;

/* Voltage option, value = option index on LCD */
typedef enum
{
  EN_0V = 0x00,
  EN_12V,
  EN_VOL_DEFAULT          /* No pending request */
} APP_VOLTAGE;

/* Signal option, value = option index on LCD */
typedef enum
{
  EN_ST = 0x00,
  EN_BLE,
  EN_UWB,
  EN_NOT_USE,
  EN_SIG_DEFAULT          /* No pending request */
} APP_SIGNAL;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Voltage divider: R55=20K (top), R58=2K (bottom) -> VBAT = VIN_MCU x 11 */
#define VBAT_DIVIDER_RATIO      11UL
/* ACS712 5A: sensitivity=185mV/A, zero-current offset=2500mV */
#define ACS712_SENSITIVITY      185L    /* mV per Ampere */
#define ACS712_OFFSET_MV        2500L   /* mV at zero current */
/* ADC reference, resolution and conversion timeout */
#define ADC_VREF_MV             3300UL
#define ADC_RESOLUTION          4095UL
#define ADC_TIMEOUT_MS          10U
/* Voltage/current update period (ms) */
#define ADC_UPDATE_PERIOD_MS    500U
/* Button debounce time (ms) */
#define BTN_DEBOUNCE_MS         5U

/* Number of options on manual screens */
#define VOL_OPTION_NUM          2U
#define SIG_OPTION_NUM          4U

/* App screen: value column and field width */
#define APP_VALUE_COL           9U
#define APP_VALUE_WIDTH         7U

/* Commands from App (1 byte each) */
#define APP_CMD_CONNECT         'c'
#define APP_CMD_DISCONNECT      'd'
#define APP_CMD_VOL_0V          '1'
#define APP_CMD_VOL_12V         '2'
#define APP_CMD_SIG_ST          '4'
#define APP_CMD_SIG_BLE         '5'
#define APP_CMD_SIG_UWB         '6'
#define APP_CMD_SIG_NOT_USE     '7'
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
static CLCD_Name LCD1;

/* Shared with USB Rx interrupt */
static volatile SETTING_STATE enl_setting_state;    /* Manual mode screen */
static volatile SETTING_APP   Is_App_Connect;
static volatile APP_VOLTAGE   App_Voltage;          /* Pending voltage request from App */
static volatile APP_SIGNAL    App_Signal;           /* Pending signal request from App */
static volatile uint8_t       Is_App_Screen_Ready;  /* 0: App screen must be (re)drawn */

/* Cursor position of options on LCD (x,y) */
static const uint8_t u1l_LCD_pos_Vol[VOL_OPTION_NUM][2] =
{
  { 1U, 1U},    /* OFF */
  { 1U, 2U}     /* ON  */
};

static const uint8_t u1l_LCD_pos_Sig[SIG_OPTION_NUM][2] =
{
  { 1U, 1U},    /* ST uC   */
  { 1U, 2U},    /* BLE     */
  { 9U, 1U},    /* UWB     */
  { 9U, 2U}     /* Not use */
};

static uint8_t u1l_LCD_pos_cur;     /* Selected option on manual screen */

/* Text on App screen, indexed by APP_VOLTAGE / APP_SIGNAL */
static const char *const App_Voltage_Text[] = { "0V", "12V" };
static const char *const App_Signal_Text[]  = { "ST uC", "BLE", "UWB", "NOT USE" };
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
/* App command */
static void f_handle_app_cmd(uint8_t cmd);
/* Mode processing */
static void f_manual_mode_process(void);
static void f_app_mode_process(void);
/* Screens */
static void f_enter_voltage_screen(void);
static void f_enter_signal_screen(void);
static void f_enter_app_screen(void);
static void f_draw_voltage_screen(void);
static void f_draw_signal_screen(void);
static void f_draw_app_screen(void);
static void f_pos_lcd(const uint8_t u1l_LCD_pos[][2], uint8_t num, uint8_t row);
static void f_lcd_write_field(uint8_t x, uint8_t y, uint8_t width, const char *text);
/* Output control */
static void f_apply_voltage(APP_VOLTAGE voltage);
static void f_apply_signal(APP_SIGNAL signal);
static void f_active_sw(void);
static void f_deactive_sw(void);
/* Button and timing */
static uint8_t f_is_button_pressed(GPIO_TypeDef *port, uint16_t pin);
static void f_wait_button_release(GPIO_TypeDef *port, uint16_t pin);
static uint8_t f_is_period_elapsed(uint32_t *last_tick, uint32_t period);
/* Voltage and current */
static uint16_t f_read_adc_ch(uint32_t channel);
static void f_read_vol_cur(uint32_t *vbat_h, uint32_t *i_h);
static void f_update_adc_display(uint32_t vbat_h, uint32_t i_h);
static void f_send_adc_app(uint32_t vbat_h, uint32_t i_h);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief  Handle data from App (called from USB Rx interrupt).
 *         Each byte is one command.
 */
void USBRxHandler(uint8_t* buf, uint32_t len)
{
  for (uint32_t i = 0U; i < len; i++)
  {
    f_handle_app_cmd(buf[i]);
  }
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
  MX_TIM3_Init();
  MX_USB_DEVICE_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  /* Calibrate ADC (important on STM32F1 for offset accuracy) */
  HAL_ADCEx_Calibration_Start(&hadc1);
  /* ADJ for LCD */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_2, 50);

  /* Init LCD */
  CLCD_4BIT_Init(&LCD1, 20, 4, LCD_RS_GPIO_Port, LCD_RS_Pin, LCD_EN_GPIO_Port, LCD_EN_Pin,
                  LCD_D4_GPIO_Port, LCD_D4_Pin, LCD_D5_GPIO_Port, LCD_D5_Pin,
                  LCD_D6_GPIO_Port, LCD_D6_Pin, LCD_D7_GPIO_Port, LCD_D7_Pin);

  /* Init setting for system */
  Is_App_Connect      = DIS_APP;
  Is_App_Screen_Ready = 0U;
  App_Voltage         = EN_VOL_DEFAULT;
  App_Signal          = EN_SIG_DEFAULT;
  f_enter_voltage_screen();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (Is_App_Connect == DIS_APP)
    {
      f_manual_mode_process();
    }
    else
    {
      f_app_mode_process();
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
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

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */
  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 7199;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 99;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LCD_D7_Pin|LCD_D6_Pin|LCD_D5_Pin|LCD_D4_Pin
                          |LCD_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LCD_RS_Pin|JTAG_EN_n_Pin|JTAG_S0_Pin|JTAG_S1_Pin
                          |ST_Prog_Ctrl_Pin|BLE_Prog_Ctrl_Pin|UWB_Prog_Ctrl_Pin|EN_24V_n_Pin
                          |EN_12V_n_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LCD_D7_Pin LCD_D6_Pin LCD_D5_Pin LCD_D4_Pin
                           LCD_EN_Pin */
  GPIO_InitStruct.Pin = LCD_D7_Pin|LCD_D6_Pin|LCD_D5_Pin|LCD_D4_Pin
                          |LCD_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LCD_RS_Pin JTAG_EN_n_Pin JTAG_S0_Pin JTAG_S1_Pin
                           ST_Prog_Ctrl_Pin BLE_Prog_Ctrl_Pin UWB_Prog_Ctrl_Pin EN_24V_n_Pin
                           EN_12V_n_Pin */
  GPIO_InitStruct.Pin = LCD_RS_Pin|JTAG_EN_n_Pin|JTAG_S0_Pin|JTAG_S1_Pin
                          |ST_Prog_Ctrl_Pin|BLE_Prog_Ctrl_Pin|UWB_Prog_Ctrl_Pin|EN_24V_n_Pin
                          |EN_12V_n_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN1_Pin BTN2_Pin BTN3_Pin */
  GPIO_InitStruct.Pin = BTN1_Pin|BTN2_Pin|BTN3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
/* ===================== App command ===================== */

/**
 * @brief  Handle one command byte from App.
 *         Control commands are accepted only after App connect.
 */
static void f_handle_app_cmd(uint8_t cmd)
{
  switch (cmd)
  {
    case APP_CMD_CONNECT:
      Is_App_Connect      = CON_APP;
      Is_App_Screen_Ready = 0U;
      break;

    case APP_CMD_DISCONNECT:
      Is_App_Connect    = DIS_APP;
      enl_setting_state = EN_DEFAULT;
      break;

    default:
      if (Is_App_Connect == CON_APP)
      {
        switch (cmd)
        {
          case APP_CMD_VOL_0V:      App_Voltage = EN_0V;      break;
          case APP_CMD_VOL_12V:     App_Voltage = EN_12V;     break;
          case APP_CMD_SIG_ST:      App_Signal  = EN_ST;      break;
          case APP_CMD_SIG_BLE:     App_Signal  = EN_BLE;     break;
          case APP_CMD_SIG_UWB:     App_Signal  = EN_UWB;     break;
          case APP_CMD_SIG_NOT_USE: App_Signal  = EN_NOT_USE; break;
          default:                  /* Unknown command */     break;
        }
      }
      break;
  }
}

/* ===================== Mode processing ===================== */

/**
 * @brief  Manual mode: control by buttons
 *         BTN3: select next option, BTN2: switch between voltage/signal screen
 */
static void f_manual_mode_process(void)
{
  static uint32_t u4l_last_adc_tick = 0U;

  switch (enl_setting_state)
  {
    case EN_VOLTAGE:
      /* Update voltage and current on LCD */
      if (f_is_period_elapsed(&u4l_last_adc_tick, ADC_UPDATE_PERIOD_MS) != 0U)
      {
        uint32_t u4l_vbat_h, u4l_i_h;
        f_read_vol_cur(&u4l_vbat_h, &u4l_i_h);
        f_update_adc_display(u4l_vbat_h, u4l_i_h);
      }

      if (f_is_button_pressed(BTN3_GPIO_Port, BTN3_Pin) != 0U)
      {
        u1l_LCD_pos_cur = (uint8_t)((u1l_LCD_pos_cur + 1U) % VOL_OPTION_NUM);
        f_pos_lcd(u1l_LCD_pos_Vol, VOL_OPTION_NUM, u1l_LCD_pos_cur);
        f_apply_voltage((APP_VOLTAGE)u1l_LCD_pos_cur);
        f_wait_button_release(BTN3_GPIO_Port, BTN3_Pin);
      }

      if (f_is_button_pressed(BTN2_GPIO_Port, BTN2_Pin) != 0U)
      {
        f_enter_signal_screen();
        f_wait_button_release(BTN2_GPIO_Port, BTN2_Pin);
      }
      break;

    case EN_SWITCH_SIGNAL:
      if (f_is_button_pressed(BTN3_GPIO_Port, BTN3_Pin) != 0U)
      {
        u1l_LCD_pos_cur = (uint8_t)((u1l_LCD_pos_cur + 1U) % SIG_OPTION_NUM);
        f_pos_lcd(u1l_LCD_pos_Sig, SIG_OPTION_NUM, u1l_LCD_pos_cur);
        f_apply_signal((APP_SIGNAL)u1l_LCD_pos_cur);
        f_wait_button_release(BTN3_GPIO_Port, BTN3_Pin);
      }

      if (f_is_button_pressed(BTN2_GPIO_Port, BTN2_Pin) != 0U)
      {
        f_enter_voltage_screen();
        f_wait_button_release(BTN2_GPIO_Port, BTN2_Pin);
      }
      break;

    default:
      /* Back from App mode */
      f_enter_voltage_screen();
      break;
  }
}

/**
 * @brief  App mode: apply requests from App and send voltage/current to App
 */
static void f_app_mode_process(void)
{
  static uint32_t u4l_last_app_tick = 0U;
  APP_VOLTAGE enl_voltage;
  APP_SIGNAL  enl_signal;

  if (Is_App_Screen_Ready == 0U)
  {
    Is_App_Screen_Ready = 1U;
    f_enter_app_screen();
  }

  enl_voltage = App_Voltage;
  if (enl_voltage != EN_VOL_DEFAULT)
  {
    App_Voltage = EN_VOL_DEFAULT;
    f_apply_voltage(enl_voltage);
    f_lcd_write_field(APP_VALUE_COL, 1U, APP_VALUE_WIDTH, App_Voltage_Text[enl_voltage]);
  }

  enl_signal = App_Signal;
  if (enl_signal != EN_SIG_DEFAULT)
  {
    App_Signal = EN_SIG_DEFAULT;
    f_apply_signal(enl_signal);
    f_lcd_write_field(APP_VALUE_COL, 2U, APP_VALUE_WIDTH, App_Signal_Text[enl_signal]);
  }

  if (f_is_period_elapsed(&u4l_last_app_tick, ADC_UPDATE_PERIOD_MS) != 0U)
  {
    uint32_t u4l_vbat_h, u4l_i_h;
    f_read_vol_cur(&u4l_vbat_h, &u4l_i_h);
    f_send_adc_app(u4l_vbat_h, u4l_i_h);
  }
}

/* ===================== Screens ===================== */

/* Voltage screen, power OFF, signal switch disabled */
static void f_enter_voltage_screen(void)
{
  enl_setting_state = EN_VOLTAGE;
  u1l_LCD_pos_cur   = 0U;
  f_apply_voltage(EN_0V);
  f_deactive_sw();
  f_draw_voltage_screen();
}

/* Signal screen, ST uC selected, signal switch enabled */
static void f_enter_signal_screen(void)
{
  enl_setting_state = EN_SWITCH_SIGNAL;
  u1l_LCD_pos_cur   = 0U;
  f_apply_signal(EN_ST);
  f_active_sw();
  f_draw_signal_screen();
}

/* App screen, power OFF, ST uC selected, signal switch enabled */
static void f_enter_app_screen(void)
{
  f_apply_voltage(EN_0V);
  f_apply_signal(EN_ST);
  f_active_sw();
  f_draw_app_screen();
}

static void f_draw_voltage_screen(void)
{
  CLCD_Clear(&LCD1);
  CLCD_SetCursor(&LCD1, 0, 0);
  CLCD_WriteString(&LCD1, "Power enable");
  CLCD_SetCursor(&LCD1, 3, 1);
  CLCD_WriteString(&LCD1, "OFF");
  CLCD_SetCursor(&LCD1, 3, 2);
  CLCD_WriteString(&LCD1, "ON");
  CLCD_SetCursor(&LCD1, 0, 3);
  CLCD_WriteString(&LCD1, "Vol:       Cur:    ");
  f_pos_lcd(u1l_LCD_pos_Vol, VOL_OPTION_NUM, 0U);
}

static void f_draw_signal_screen(void)
{
  CLCD_Clear(&LCD1);
  CLCD_SetCursor(&LCD1, 0, 0);
  CLCD_WriteString(&LCD1, "Switch signal");
  CLCD_SetCursor(&LCD1, 3, 1);
  CLCD_WriteString(&LCD1, "ST uC");
  CLCD_SetCursor(&LCD1, 11, 1);
  CLCD_WriteString(&LCD1, "UWB");
  CLCD_SetCursor(&LCD1, 3, 2);
  CLCD_WriteString(&LCD1, "BLE");
  CLCD_SetCursor(&LCD1, 11, 2);
  CLCD_WriteString(&LCD1, "Not use");
  f_pos_lcd(u1l_LCD_pos_Sig, SIG_OPTION_NUM, 0U);
}

static void f_draw_app_screen(void)
{
  CLCD_Clear(&LCD1);
  CLCD_SetCursor(&LCD1, 0, 0);
  CLCD_WriteString(&LCD1, "Connected JIG App");
  CLCD_SetCursor(&LCD1, 0, 1);
  CLCD_WriteString(&LCD1, "Voltage: ");
  CLCD_SetCursor(&LCD1, 0, 2);
  CLCD_WriteString(&LCD1, "Signal : ");
}

/* Move '>' cursor to option 'row' */
static void f_pos_lcd(const uint8_t u1l_LCD_pos[][2], uint8_t num, uint8_t row)
{
  for (uint8_t i = 0U; i < num; i++)
  {
    CLCD_SetCursor(&LCD1, u1l_LCD_pos[i][0], u1l_LCD_pos[i][1]);
    CLCD_WriteString(&LCD1, " ");
  }
  CLCD_SetCursor(&LCD1, u1l_LCD_pos[row][0], u1l_LCD_pos[row][1]);
  CLCD_WriteString(&LCD1, ">");
}

/* Clear 'width' characters at (x,y) then write text */
static void f_lcd_write_field(uint8_t x, uint8_t y, uint8_t width, const char *text)
{
  CLCD_SetCursor(&LCD1, x, y);
  for (uint8_t i = 0U; i < width; i++)
  {
    CLCD_WriteChar(&LCD1, ' ');
  }
  CLCD_SetCursor(&LCD1, x, y);
  CLCD_WriteString(&LCD1, (char *)text);
}

/* ===================== Output control ===================== */

static void f_apply_voltage(APP_VOLTAGE voltage)
{
  switch (voltage)
  {
    case EN_0V:
      HAL_GPIO_WritePin(EN_12V_n_GPIO_Port, EN_12V_n_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(EN_24V_n_GPIO_Port, EN_24V_n_Pin, GPIO_PIN_RESET);
      break;

    case EN_12V:
      HAL_GPIO_WritePin(EN_12V_n_GPIO_Port, EN_12V_n_Pin, GPIO_PIN_SET);
      break;

    default:
      break;
  }
}

/* JTAG mux select (S1 S0): 00 ST uC, 01 BLE, 10 UWB, 11 Not use */
static void f_apply_signal(APP_SIGNAL signal)
{
  static const GPIO_PinState sel[SIG_OPTION_NUM][2] =
  {
    /*  S0              S1           */
    { GPIO_PIN_RESET, GPIO_PIN_RESET },   /* ST uC   */
    { GPIO_PIN_SET,   GPIO_PIN_RESET },   /* BLE     */
    { GPIO_PIN_RESET, GPIO_PIN_SET   },   /* UWB     */
    { GPIO_PIN_SET,   GPIO_PIN_SET   }    /* Not use */
  };

  if (signal < EN_SIG_DEFAULT)
  {
    HAL_GPIO_WritePin(JTAG_S0_GPIO_Port, JTAG_S0_Pin, sel[signal][0]);
    HAL_GPIO_WritePin(JTAG_S1_GPIO_Port, JTAG_S1_Pin, sel[signal][1]);
  }
}

static void f_active_sw(void)
{
  HAL_GPIO_WritePin(JTAG_EN_n_GPIO_Port, JTAG_EN_n_Pin, GPIO_PIN_RESET);
}

static void f_deactive_sw(void)
{
  HAL_GPIO_WritePin(JTAG_EN_n_GPIO_Port, JTAG_EN_n_Pin, GPIO_PIN_SET);
  f_apply_signal(EN_ST);
}

/* ===================== Button and timing ===================== */

/* Return 1 if button (active low) is pressed after debounce */
static uint8_t f_is_button_pressed(GPIO_TypeDef *port, uint16_t pin)
{
  if (HAL_GPIO_ReadPin(port, pin) != GPIO_PIN_RESET)
  {
    return 0U;
  }
  HAL_Delay(BTN_DEBOUNCE_MS);
  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void f_wait_button_release(GPIO_TypeDef *port, uint16_t pin)
{
  while (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
  {
  }
}

/* Return 1 (and restart period) if 'period' ms elapsed since *last_tick */
static uint8_t f_is_period_elapsed(uint32_t *last_tick, uint32_t period)
{
  uint32_t now = HAL_GetTick();
  if ((now - *last_tick) >= period)
  {
    *last_tick = now;
    return 1U;
  }
  return 0U;
}

/* ===================== Voltage and current ===================== */

/**
 * @brief  Switch ADC channel, perform one conversion and return raw value.
 */
static uint16_t f_read_adc_ch(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  uint16_t val;

  sConfig.Channel      = channel;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, ADC_TIMEOUT_MS);
  val = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return val;
}

/**
 * @brief  Read VIN_MCU (CH0) and IIN_MCU (CH1) and convert to real values.
 *         Outputs are in hundredths: vbat_h = 1234 -> 12.34V, i_h = 123 -> 1.23A
 *
 * Voltage: VBAT = VIN_MCU x (R55+R58)/R58 = VIN_MCU x 11
 * Current: I = (Vout - 2500mV) / 185mV  (ACS712 5A)
 */
static void f_read_vol_cur(uint32_t *vbat_h, uint32_t *i_h)
{
  uint16_t adc_vin = f_read_adc_ch(ADC_CHANNEL_0);
  *vbat_h = (uint32_t)adc_vin * ADC_VREF_MV * VBAT_DIVIDER_RATIO / ADC_RESOLUTION / 10U;

  uint16_t adc_iin = f_read_adc_ch(ADC_CHANNEL_1);
  int32_t  vout_mv = (int32_t)((uint32_t)adc_iin * ADC_VREF_MV / ADC_RESOLUTION);
  int32_t  i_ma    = (vout_mv - ACS712_OFFSET_MV) * 1000L / ACS712_SENSITIVITY;
  if (i_ma < 0)
  {
    i_ma = 0;
  }
  *i_h = (uint32_t)i_ma / 10U;
}

/**
 * @brief  Update row 3 of LCD: "Vol: XX.XXV     Cur: X.XXA"
 */
static void f_update_adc_display(uint32_t vbat_h, uint32_t i_h)
{
  char buf[8];

  (void)snprintf(buf, sizeof(buf), "%02lu.%02luV",
                 (unsigned long)(vbat_h / 100U), (unsigned long)(vbat_h % 100U));
  CLCD_SetCursor(&LCD1, 4, 3);
  CLCD_WriteString(&LCD1, buf);

  (void)snprintf(buf, sizeof(buf), "%lu.%02luA",
                 (unsigned long)(i_h / 100U), (unsigned long)(i_h % 100U));
  CLCD_SetCursor(&LCD1, 15, 3);
  CLCD_WriteString(&LCD1, buf);
}

/**
 * @brief  Send voltage and current to App over USB CDC.
 *         Frame format: "V:12.34,I:1.23\r\n"
 */
static void f_send_adc_app(uint32_t vbat_h, uint32_t i_h)
{
  /* Static: USB transmits asynchronously from this buffer */
  static char tx_buf[32];
  int len;

  len = snprintf(tx_buf, sizeof(tx_buf), "V:%lu.%02lu,I:%lu.%02lu\r\n",
                 (unsigned long)(vbat_h / 100U), (unsigned long)(vbat_h % 100U),
                 (unsigned long)(i_h / 100U), (unsigned long)(i_h % 100U));
  if (len > 0)
  {
    /* Skip this frame if USB is busy, next period will send again */
    (void)CDC_Transmit_FS((uint8_t *)tx_buf, (uint16_t)len);
  }
}

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

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
