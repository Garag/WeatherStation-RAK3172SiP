/*
 * coworker.c
 *
 *  Created on: Mar 14, 2023
 *      Author: dirki
 */

#include "coworker.h"
#include "flashdata.h"
#include "usart.h"
#include "sys_app.h"

#define COWORKER_RESET 0
#define COWORKER_RESET_BOOTLOADER 1

const uint8_t BOOT_SYNC = 0x7F;
const uint8_t BOOT_ACK = 0x79;
const uint8_t BOOT_NACK = 0x1F;

const uint8_t BOOT_CMD_GET[] = { 0x00, 0xFF };
const uint8_t BOOT_CMD_GO[] = { 0x21, 0xDE };
const uint8_t BOOT_CMD_EXT_ERASE[] = { 0x44, 0xBB };
const uint8_t BOOT_CMD_READ[] = { 0x11, 0xEE };
const uint8_t BOOT_CMD_WRITE[] = { 0x31, 0xCE };

const uint8_t FLASH_START_ADR_WITH_CSUM[] = { 0x08, 0x00, 0x00, 0x00, 0x08 };
const uint8_t EXT_ERASE_CHIP[] = { 0x00, 0xFF, 0xFF };


static HAL_StatusTypeDef CoWorker_Flash_Firmware(const uint8_t *pFirmware, uint32_t len);
static HAL_StatusTypeDef CoWorker_CMD_Get(void);
static HAL_StatusTypeDef CoWorker_CMD_Run(void);
static HAL_StatusTypeDef CoWorker_CMD_ExtErase(void);
static HAL_StatusTypeDef CoWorker_CMD_Write(uint32_t address, const uint8_t *pData, uint32_t size);
static HAL_StatusTypeDef CoWorker_Write_Data(void);

static HAL_StatusTypeDef CoWorker_Flash_Sync(void);
static void CoWorker_ResetSequence(uint32_t bIntoBootloader);
static HAL_StatusTypeDef CoWorker_Wait_ACK(uint32_t timeout);
static HAL_StatusTypeDef CoWorker_Send_CMD(const uint8_t *pCmd);
static HAL_StatusTypeDef CoWorker_Send_Address(uint32_t adr);
static HAL_StatusTypeDef CoWorker_Send_Data(const uint8_t *pData, uint32_t size);

void CoWorker_Init(void) {
	HAL_StatusTypeDef status = HAL_OK;
    uint8_t startupMsg[16];

    CoWorker_ResetSequence(COWORKER_RESET);

	status = HAL_UART_Receive(&huart1, startupMsg, 8, 2);

    if (HAL_OK != status || startupMsg[0] != 0x09) {
        // Missing or incorrect startup message from coworker. Try to flash with new firmware.
        CoWorker_Flash_Firmware(flashDataL031, sizeof(flashDataL031));
    }
}

uint32_t CoWorker_GetCounter(void) {
	return 0;
}


static HAL_StatusTypeDef CoWorker_Flash_Firmware(const uint8_t *pFirmware, uint32_t len) {
	uint32_t cnt = 3;

	while(cnt) {
		if (HAL_OK == CoWorker_Flash_Sync())
			break;
		--cnt;
		APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware retry count %d\r\n", cnt);
	}

	if (cnt == 0)
		return HAL_ERROR;

//    if (res == HAL_OK) res = CoWorker_CMD_Get();

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware erase chip ...\r\n");
    if (HAL_OK != CoWorker_CMD_ExtErase()) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware erase chip FAILED!\r\n");
        return HAL_ERROR;
    }

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware flash firmware ...\r\n");
    if (HAL_OK != CoWorker_Write_Data()) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware flash firmware FAILED!\r\n");
        return HAL_ERROR;
    }
    // verify firmware

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware run ...\r\n");
    if (HAL_OK != CoWorker_CMD_Run()) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Firmware run FAILED!\r\n");
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef CoWorker_Flash_Sync(void) {

    CoWorker_ResetSequence(COWORKER_RESET_BOOTLOADER);
    HAL_UART_Transmit(&huart1, &BOOT_SYNC, 1, 10);
    HAL_Delay(10);

    uint8_t response = 0;
    if (HAL_OK != HAL_UART_Receive(&huart1, &response, 1, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Sync failed to receive response\r\n");
        return HAL_ERROR;
    }

    if (BOOT_ACK != response) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Sync receive 0x%02x\r\n", response);
        return HAL_ERROR;
    }

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Flash_Sync flash sync ok\r\n");
    return HAL_OK;
}

static void CoWorker_ResetSequence(uint32_t bIntoBootloader) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // reset CoWorker
    HAL_GPIO_WritePin(coworkerReset_GPIO_Port, coworkerReset_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = coworkerReset_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(coworkerReset_GPIO_Port, &GPIO_InitStruct);

    if (bIntoBootloader) {
        // set Boot0 = 1 to enter system bootloader
        HAL_GPIO_WritePin(coworkerBoot_GPIO_Port, coworkerBoot_Pin, GPIO_PIN_SET);
        GPIO_InitStruct.Pin = coworkerBoot_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(coworkerBoot_GPIO_Port, &GPIO_InitStruct);
    }

    HAL_Delay(10);
    HAL_UART_AbortReceive(&huart1);

    // let CoWorker run (system bootloader)
    GPIO_InitStruct.Pin = coworkerReset_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_Delay(10);

    if (bIntoBootloader) {
        // configure Boot0 pin back to analog input
        GPIO_InitStruct.Pin = coworkerBoot_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

static HAL_StatusTypeDef CoWorker_Wait_ACK(uint32_t timeout) {
    uint8_t response = 0;
    if (HAL_OK != HAL_UART_Receive(&huart1, &response, 1, timeout)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Wait_ACK failed to receive ACK/NACK\r\n");
        return HAL_ERROR;
    }

    if (response == BOOT_NACK) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Wait_ACK NACK received\r\n");
        return HAL_ERROR;
    }

    if (response != BOOT_ACK) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Wait_ACK unexpected response received\r\n");
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef CoWorker_Send_CMD(const uint8_t *pCmd) {
    if (HAL_OK != HAL_UART_Transmit(&huart1, pCmd, 2, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_CMD send failed\r\n");
        return HAL_ERROR;
    }

    return CoWorker_Wait_ACK(10);
}

static HAL_StatusTypeDef CoWorker_Send_Address(uint32_t adr) {
    uint8_t msg[5];

    msg[0] = (uint8_t)(adr >> 24);
    msg[1] = (uint8_t)(adr >> 16);
    msg[2] = (uint8_t)(adr >>  8);
    msg[3] = (uint8_t)adr;
    msg[4] = msg[0] ^ msg[1] ^ msg[2] ^ msg[3];

    if (HAL_OK != HAL_UART_Transmit(&huart1, msg, 5, 20)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_Address send failed\r\n");
        return HAL_ERROR;
    }

    return CoWorker_Wait_ACK(10);
}

static HAL_StatusTypeDef CoWorker_Send_Data(const uint8_t *pData, uint32_t size) {

    if (size & 0x03) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_Data %d not a multiple of 4!\r\n", size);
        size = ((size >> 2) + 1) << 2;

    }

    uint8_t numberBytes = (uint8_t)(size-1);
    uint8_t checksum = numberBytes;

    for(int n=0; n<size; n++) {
        checksum ^= pData[n];
    }

    if (HAL_OK != HAL_UART_Transmit(&huart1, &numberBytes, 1, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_Data send N failed\r\n");
        return HAL_ERROR;
    }
    if (HAL_OK != HAL_UART_Transmit(&huart1, pData, size, 200)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_Data send data failed\r\n");
        return HAL_ERROR;
    }

    if (HAL_OK != HAL_UART_Transmit(&huart1, &checksum, 1, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Send_Data send checksum failed\r\n");
        return HAL_ERROR;
    }

    return CoWorker_Wait_ACK(1000);
}

static HAL_StatusTypeDef CoWorker_CMD_Get(void) {

    if (HAL_OK != CoWorker_Send_CMD(BOOT_CMD_GET)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get failed\r\n");
        return HAL_ERROR;
    }

    uint8_t numberOfBytes=0;
    if (HAL_OK != HAL_UART_Receive(&huart1, &numberOfBytes, 1, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get failed to receive N\r\n");
        return HAL_ERROR;
    }

    uint8_t protVersion=0;
    if (HAL_OK != HAL_UART_Receive(&huart1, &protVersion, 1, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get failed to receive protocol version\r\n");
        return HAL_ERROR;
    }

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get protocol version = 0x%02x\r\n", protVersion);

    uint8_t supportedCmd=0;
    do {
        if (HAL_OK != HAL_UART_Receive(&huart1, &supportedCmd, 1, 10)) {
            APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get failed to receive next cmd\r\n");
            return HAL_ERROR;
        }
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Get cmd 0x%02x supported\r\n", supportedCmd);
    } while (--numberOfBytes > 0);

    return CoWorker_Wait_ACK(100);
}

static HAL_StatusTypeDef CoWorker_CMD_Run(void) {
    if (HAL_OK != CoWorker_Send_CMD(BOOT_CMD_GO)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Run failed\r\n");
        return HAL_ERROR;
    }

    if (HAL_OK != HAL_UART_Transmit(&huart1, FLASH_START_ADR_WITH_CSUM, sizeof(FLASH_START_ADR_WITH_CSUM), 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Run unable to send address\r\n");
        return HAL_ERROR;
    }

    return CoWorker_Wait_ACK(100);
}

static HAL_StatusTypeDef CoWorker_CMD_ExtErase(void) {
    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase send CMD\r\n");
    if (HAL_OK != CoWorker_Send_CMD(BOOT_CMD_EXT_ERASE)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase failed\r\n");
        return HAL_ERROR;
    }

    uint8_t checksum = 0;
    uint8_t cmdBuf[2];
    uint16_t usedPages = ((sizeof(flashDataL031)+127) >> 7) - 1;
    cmdBuf[0] = (uint8_t)(usedPages >> 8);
    cmdBuf[1] = (uint8_t)(usedPages & 0xFF);
    checksum = cmdBuf[0] ^ cmdBuf[1];

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase send erase (size=%d, N=%d)\r\n", sizeof(flashDataL031), usedPages );
    if (HAL_OK != HAL_UART_Transmit(&huart1, cmdBuf, 2, 10)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase unable to send N\r\n");
        return HAL_ERROR;
    }

    uint8_t pageRaw[2];
    uint32_t pageNr;
    for (pageNr = 0; pageNr <= usedPages; ++pageNr) {
        pageRaw[0] = (uint8_t)(pageNr >> 8);
        pageRaw[1] = (uint8_t)(pageNr & 0xFF);
        checksum = checksum ^ pageRaw[0] ^ pageRaw[1];
        if (HAL_OK != HAL_UART_Transmit(&huart1, pageRaw, 2, 10)) {
            APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase unable to send page id\r\n");
            return HAL_ERROR;
        }
    }
    if (HAL_OK != HAL_UART_Transmit(&huart1, &checksum, 1, 5)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase unable to send checksum\r\n");
        return HAL_ERROR;
    }

    APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_ExtErase wait for completion\r\n");
    return CoWorker_Wait_ACK(5000);
}

static HAL_StatusTypeDef CoWorker_CMD_Write(uint32_t address, const uint8_t *pData, uint32_t size) {
    if (HAL_OK != CoWorker_Send_CMD(BOOT_CMD_WRITE)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Write failed\r\n");
        return HAL_ERROR;
    }

    if (HAL_OK != CoWorker_Send_Address(address)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Write send address failed\r\n");
        return HAL_ERROR;
    }

    if (HAL_OK != CoWorker_Send_Data(pData, size)) {
        APP_LOG(TS_ON, VLEVEL_M, "CoWorker_CMD_Write failed\r\n");
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef CoWorker_Write_Data(void) {
    uint32_t currentAddress = 0x08000000;
    uint32_t size = sizeof(flashDataL031);
    const uint8_t *pData = flashDataL031;

    while (size >= 256) {
        if (HAL_OK != CoWorker_CMD_Write(currentAddress, pData, 256)) {
            APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Write_Data failed at 0x%08x\r\n", currentAddress);
            return HAL_ERROR;
        }
        size -= 256;
        pData += 256;
        currentAddress += 256;
    }

    if (size > 0) {
        if (HAL_OK != CoWorker_CMD_Write(currentAddress, pData, size)) {
            APP_LOG(TS_ON, VLEVEL_M, "CoWorker_Write_Data failed at 0x%08x\r\n", currentAddress);
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}