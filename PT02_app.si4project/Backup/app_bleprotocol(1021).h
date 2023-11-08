/*
 * app_bleprotocol.h
 *
 *  Created on: Oct 28, 2023
 *      Author: nimo
 */

#ifndef TASK_INC_APP_BLEPROTOCOL_H_
#define TASK_INC_APP_BLEPROTOCOL_H_

#include <config.h>

#define BLE_CONNECT_LIST_SIZE   2

/* 蓝牙通讯协议 */
#define CMD_DEV_INFO_PARAM                  0x30  //蓝牙上报信息
#define CMD_DEV_HEARTBEAT					0x31  //心跳


/* 蓝牙发数据协议 */
#define BLE_SEND_INFO_EVENT                 0x00000001 //设备上报信息



void bleProtoclRecvParser(uint8_t connHandle, uint8_t *data, uint8_t len);

#endif /* TASK_INC_APP_BLEPROTOCOL_H_ */
