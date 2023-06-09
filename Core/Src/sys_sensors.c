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
#include "lptim.h"
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

#ifdef USE_BME680
static struct bme68x_dev bme;
static uint8_t dev_addr;
#endif

static uint32_t rainCounterLastValue = 0;
static uint32_t windCounterLastValue = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

#ifdef USE_BME680
    static BME68X_INTF_RET_TYPE bme68x_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr);
    static BME68X_INTF_RET_TYPE bme68x_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr);
    static void bme68x_delay_us(uint32_t period, void *intf_ptr);
#else
    static uint8_t sht3x_calc_crc(const uint8_t *data, size_t length);
#endif
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
int32_t EnvSensors_Read(sensor_t *sensor_data)
{
  /* USER CODE BEGIN EnvSensors_Read */
	HAL_StatusTypeDef ret = HAL_OK;

//    MX_I2C2_Init();

	// set default values
	sensor_data->humidity    = 0.0;
	sensor_data->temperature = -45.0;
	sensor_data->pressure    = 0.0;

    uint32_t rainValue = LL_LPTIM_GetCounter(LPTIM1);
    uint32_t windValue = LL_LPTIM_GetCounter(LPTIM2);
    sensor_data->rainCounter = rainValue - rainCounterLastValue;
    sensor_data->windCounter = windValue - windCounterLastValue;
    rainCounterLastValue = rainValue;
    windCounterLastValue = windValue;

#ifdef USE_BME680
    struct bme68x_conf conf;
//    struct bme68x_heatr_conf heatr_conf;
    struct bme68x_data data;
    uint8_t n_fields;

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
#else
    uint8_t buffer[6];
    ret = HAL_I2C_Master_Transmit(&hi2c2, sht3x_addr_l << 1, (uint8_t *)cmd_single_high_stretch, sizeof(cmd_single_high_stretch), 30);
    if (ret != HAL_OK) return ret;

    HAL_Delay(1);
    ret = HAL_I2C_Master_Receive(&hi2c2, sht3x_addr_l << 1, buffer, sizeof(buffer), 30);
    if (ret != HAL_OK) return ret;

    uint8_t temp_crc = sht3x_calc_crc(buffer, 2);
    uint8_t hum_crc = sht3x_calc_crc(buffer + 3, 2);
    if (temp_crc != buffer[2] || hum_crc != buffer[5]) {
        return HAL_ERROR;
    }
    else {
        int16_t temp_raw = (int16_t)buffer[0] << 8 | (uint16_t)buffer[1];
        uint16_t hum_raw = (int16_t)buffer[3] << 8 | (uint16_t)buffer[4];

        sensor_data->temperature = -45.0f + 175.0f * (float)temp_raw / 65535.0f;
        sensor_data->humidity = 100.0f * (float)hum_raw / 65535.0f;
    }
#endif
	return ret;
  /* USER CODE END EnvSensors_Read */
}

int32_t EnvSensors_Init(void)
{
  int32_t ret = 0;
  /* USER CODE BEGIN EnvSensors_Init */
  MX_I2C2_Init();

#ifdef USE_BME680
  dev_addr = (BME68X_I2C_ADDR_HIGH) << 1;
  bme.intf_ptr = &dev_addr;
  bme.read = bme68x_read;
  bme.write = bme68x_write;
  bme.intf = BME68X_I2C_INTF;
  bme.delay_us = bme68x_delay_us;
  bme.amb_temp = 23;
#else

#endif

  /* USER CODE END EnvSensors_Init */
  return ret;
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private Functions Definition -----------------------------------------------*/
/* USER CODE BEGIN PrFD */

#ifdef USE_BME680
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

#else  // USE_BME680

static uint8_t sht3x_calc_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xff;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (size_t j = 0; j < 8; j++) {
            if ((crc & 0x80u) != 0) {
                crc = (uint8_t)((uint8_t)(crc << 1u) ^ 0x31u);
            } else {
                crc <<= 1u;
            }
        }
    }
    return crc;
}
#endif

/* USER CODE END PrFD */
