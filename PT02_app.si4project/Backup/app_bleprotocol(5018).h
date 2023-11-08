/*
 * app_bleprotocol.h
 *
 *  Created on: Oct 28, 2023
 *      Author: nimo
 */

#ifndef TASK_INC_APP_BLEPROTOCOL_H_
#define TASK_INC_APP_BLEPROTOCOL_H_

#include <config.h>


/* 蓝牙通讯协议 */
#define CMD_DEV_INFO_PARAM                  0x30  //蓝牙上报信息
#define CMD_DEV_HEARTBEAT					0x31  //心跳


/* 蓝牙发数据协议 */
#define BLE_SEND_INFO_EVENT                 0x00000001 //设备上报信息



void bleProtoclRecvParser(uint8_t connHandle, uint8_t *data, uint8_t len);
void bleProtocolSendEventTask(void);

void bleSendDataReqSet(uint8_t ind, uint32_t req);
void bleSendDataReqClear(uint8_t ind, uint32_t req);
void bleProtocolSendPeriod(void);


#endif /* TASK_INC_APP_BLEPROTOCOL_H_ */
