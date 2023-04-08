/*
 * coworker.h
 *
 *  Created on: Mar 14, 2023
 *      Author: dirki
 */

#ifndef INC_COWORKER_H_
#define INC_COWORKER_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"

void CoWorker_Init(void);

uint32_t CoWorker_GetCounter(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_COWORKER_H_ */
