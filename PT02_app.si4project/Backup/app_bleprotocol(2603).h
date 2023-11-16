/*
 * app_bleprotocol.h
 *
 *  Created on: Oct 28, 2023
 *      Author: nimo
 */

#ifndef TASK_INC_APP_BLEPROTOCOL_H_
#define TASK_INC_APP_BLEPROTOCOL_H_

#include <config.h>


/**
 * 设备连上后，主机通过CMD_DEV_LOGIN_INFO询问从机的SN号
 * 主机接收到sn号后先查看链路中是否用正在用该sn号的链路
 * 有就表示链路正在连接中只不过蓝牙链路断开了
   无则看socksuccess是否==0，0表示没链接，1表示已通过别的PT02连接
 * 主机一旦确认:1.链路中无该SN号正在连接2.有空闲链路，则把本机SN号通过CMD_DEV_MASTER_INFO发送给PT13并把socksuccess置1（无论链路是否成功登录）
 * 主机端如果三分钟内没收到PT13的回复，断开链路，并清空链路数据
 * 
 */

/* 蓝牙通讯协议 */
#define CMD_DEV_LOGIN_INFO                  0x30  //获取蓝牙登录信息
#define CMD_DEV_HEARTBEAT					0x31
#define CMD_DEV_MASTER_INFO					0x32  //主机状态


/* 蓝牙发数据协议 */
#define BLE_SEND_LOGININFO_EVENT            0x00000001 //设备上报信息
#define BLE_SEND_HBT_EVNET					0x00000002
#define BLE_SEND_MASTERINFO_EVENT			0x00000004

#define BLE_SEND_ALL_EVENT					0xFFFFFFFF



void bleProtoclRecvParser(uint8_t connHandle, uint8_t *data, uint8_t len);
void bleProtocolSendEventTask(void);

void bleSendDataReqSet(uint8_t ind, uint32_t req);
void bleSendDataReqClear(uint8_t ind, uint32_t req);
void bleProtocolSendPeriod(void);


#endif /* TASK_INC_APP_BLEPROTOCOL_H_ */
