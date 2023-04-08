/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sys_sensors.c
  * @author  MCD Application Team
  * @brief   Manages the sensors on the application
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "stdint.h"
#include "platform.h"
#include "sys_conf.h"
#include "sys_sensors.h"
#if defined (SENSOR_ENABLED) && (SENSOR_ENABLED == 0)
#include "adc_if.h"
#endif /* SENSOR_ENABLED */

/* USER CODE BEGIN Includes */
#include "sys_app.h"
#include "i2c.h"
#include "bme68x.h"
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
const uint8_t cmd_single_high_stretch[] = { 0x2c, 0x06 };
const uint8_t cmd_read_status[] = { 0xf3, 0x2d };
const uint16_t sht3x_addr_l = 0x44;
const uint16_t sht3x_addr_h = 0x45;

static struct bme68x_dev bme;
static uint8_t dev_addr;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static BME68X_INTF_RET_TYPE bme68x_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr);
static BME68X_INTF_RET_TYPE bme68x_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr);
static void bme68x_delay_us(uint32_t period, void *intf_ptr);
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
int32_t EnvSensors_Read(sensor_t *sensor_data)
{
  /* USER CODE BEGIN EnvSensors_Read */
	HAL_StatusTypeDef ret = HAL_OK;
    struct bme68x_conf conf;
//    struct bme68x_heatr_conf heatr_conf;
    struct bme68x_data data;
    uint8_t n_fields;

	// set default values
	sensor_data->humidity    = 0.0;
	sensor_data->temperature = -45.0;
	sensor_data->pressure    = 0.0;

//	MX_I2C2_Init();
//    HAL_Delay(1);

	uint8_t rslt = bme68x_init(&bme);

    conf.filter = BME68X_FILTER_OFF;
    conf.odr = BME68X_ODR_NONE;
    conf.os_hum = BME68X_OS_16X;
    conf.os_pres = BME68X_OS_1X;
    conf.os_temp = BME68X_OS_2X;
    rslt = bme68x_set_conf(&conf, &bme);
    rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme);
    HAL_Delay(1);
    rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme);

    if (rslt == BME68X_OK) {
    	sensor_data->humidity    = data.humidity;
    	sensor_data->temperature = data.temperature;
    	sensor_data->pressure    = data.pressure;
    }
	return ret;
  /* USER CODE END EnvSensors_Read */
}

int32_t EnvSensors_Init(void)
{
  int32_t ret = 0;
  /* USER CODE BEGIN EnvSensors_Init */
  MX_I2C2_Init();

  dev_addr = (BME68X_I2C_ADDR_HIGH) << 1;
  bme.intf_ptr = &dev_addr;
  bme.read = bme68x_read;
  bme.write = bme68x_write;
  bme.intf = BME68X_I2C_INTF;
  bme.delay_us = bme68x_delay_us;
  bme.amb_temp = 23;

  /* USER CODE END EnvSensors_Init */
  return ret;
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private Functions Definition -----------------------------------------------*/
/* USER CODE BEGIN PrFD */

static BME68X_INTF_RET_TYPE bme68x_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
	HAL_StatusTypeDef ret;
	uint16_t dev_addr = *(uint8_t*)intf_ptr;
	ret = HAL_I2C_Mem_Read(&hi2c2, dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, (uint8_t *)reg_data, length, 100);
	if (ret != HAL_OK) {
		return BME68X_E_COM_FAIL;
	}
	return BME68X_INTF_RET_SUCCESS;
}

static BME68X_INTF_RET_TYPE bme68x_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
	HAL_StatusTypeDef ret;
	uint16_t dev_addr = *(uint8_t*)intf_ptr;
	ret = HAL_I2C_Mem_Write(&hi2c2, dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, (uint8_t *)reg_data, length, 100);
	if (ret != HAL_OK) {
		return BME68X_E_COM_FAIL;
	}
	return BME68X_INTF_RET_SUCCESS;
}

static void bme68x_delay_us(uint32_t period, void *intf_ptr)
{
	uint32_t delay_ms = (period + 999) / 1000;
	HAL_Delay(delay_ms);
}

/* USER CODE END PrFD */
