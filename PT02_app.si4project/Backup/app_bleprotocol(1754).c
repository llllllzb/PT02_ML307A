/*
 * app_bleprotocol.c
 *
 *  Created on: Oct 28, 2023
 *      Author: nimo
 */

#include "app_bleprotocol.h"
#include "app_central.h"
#include "app_server.h"
#include "app_socket.h"
#include "app_param.h"

/**************************************************
@bref      	登录信息解析
@param
	ind			蓝牙设备信息指针
    sn			登录sn号
    connSuccess	是否已登录
    masterSn	pt13主机sn号
    bleinfo     蓝牙信息
@return
@note       
**************************************************/

static void devLoginInfoParser(uint8_t ind, char *sn, uint8_t connSuccess, char *masterSn, deviceConnInfo_s *bleinfo)
{
	char master[20] = { 0 };
	char slavor[20] = { 0 };
	memcpy(master, masterSn, 15);
	master[15] = 0;
	memcpy(slavor, sn, 15);
	slavor[15] = 0;
	/* 已存在且正在连接 */
	if (blePetSearchServerSn(slavor) >= 0)
	{
		memcpy(bleinfo->sockData.SN, slavor, 15);
		bleinfo->sockData.SN[15] = 0;
		bleinfo->sockSuccess = 1;
		bleinfo->socketId = blePetSearchServerSn(slavor);
		bleSendDataReqSet(ind, BLE_SEND_MASTERINFO_EVENT);
		LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Exist server, Sn:[%s] Socketid:%d", bleinfo->sockData.SN, bleinfo->socketId);
		return;
	}
	/* 设备已连接 */
	if (connSuccess == 1)
	{
		/* 先判断主机sn号是否是自己 */
		if (strncmp(master, dynamicParam.SN, 15) == 0)
		{
			memcpy(bleinfo->sockData.SN, slavor, 15);
			bleinfo->sockData.SN[15] = 0;
			bleinfo->sockSuccess = 1;
			bleinfo->socketId = blePetServerAdd(bleinfo->sockData.SN);
			bleSendDataReqSet(ind, BLE_SEND_MASTERINFO_EVENT);
			LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Local link, LocolSn[%s] MasterSn[%s] SlavorSn:[%s]", dynamicParam.SN, master, bleinfo->sockData.SN);
		}
		/* 进入该状态表示设备已通过别的PT02连接 */
		else
		{
			/* 从server列表删除 */
			blePetServerDel(slavor);
			/* 从socket列表删除,可以不要 */
			socketDel(blePetSearchServerSn(slavor));
			/* 从blecon列表删除，可加可不加，加的话会一直重连 */

			LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Other link, LocolSn[%s] MasterSn:[%s] SlavorSn:[%s]", dynamicParam.SN, master, slavor);
 			
		}
		return;
	}
	/* 未连接且本机不存在该sn的链路，查询空闲链路 */
	if (blePetSearchIdleServer() < 0)
	{
		LogPrintf(DEBUG_BLE, "$$ Ble get login info==>No idle server");
		return;
	}
	/* 插入连接 */
	memcpy(bleinfo->sockData.SN, slavor, 15);
	bleinfo->sockData.SN[15] = 0;
	bleinfo->sockSuccess = 1;
	bleinfo->socketId = blePetServerAdd(bleinfo->sockData.SN);
	bleSendDataReqSet(ind, BLE_SEND_MASTERINFO_EVENT);
	/* 确认这里是否要删除链路 */
	socketDel(bleinfo->socketId);
	LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Add ble server, Sn:[%s] Socketid:%d", bleinfo->sockData.SN, bleinfo->socketId);
	
}

/**************************************************
@bref      	上报信息录入
@param
    sn			登录sn号
    sockFlag	是否已登录
@return
@note       
**************************************************/

static void devHbtParser(deviceConnInfo_s *bleinfo, float vol, uint8_t bat, uint16_t step)
{
	bleinfo->sockData.vol = vol;
	bleinfo->sockData.bat = bat;
	bleinfo->sockData.step = step;
}


/**************************************************
@bref       蓝牙协议解析
@param
    data
    len
@return
@note       0C 04 80 09 04 E7 78 0D
**************************************************/

void bleProtoclRecvParser(uint8_t connHandle, uint8_t *data, uint8_t len)
{
    uint8_t readInd, size, crc, i;
    uint8_t *addr;
    char debug[20] = { 0 };
    char sn[16] = { 0 };
    uint16_t value16;
    float valuef;
    int8_t ind, sockid;
    deviceConnInfo_s *bleInfo;
    if (len <= 5)
    {
        return;
    }
    bleInfo = bleDevGetInfoByHandle(connHandle);
    if (bleInfo == NULL)
    {
    	LogPrintf(DEBUG_BLE, "$$ Ble list can not find this device,conhandle:%d", connHandle);
        return;
    }
    ind = bleDevGetIndexByHandle(connHandle);
	if (ind == -1)
	{
		LogPrintf(DEBUG_BLE, "$$ Ble list can not find this device, conhandle:%d", connHandle);
		return;
	}
    for (readInd = 0; readInd < len; readInd++)
    {
        if (data[readInd] != 0x0C)
        {
            continue;
        }
        if (readInd + 4 >= len)
        {
            //内容超长了
            break;
        }
        size = data[readInd + 1];
        if (readInd + 3 + size >= len)
        {
            continue;
        }
        if (data[readInd + 3 + size] != 0x0D)
        {
            continue;
        }
        crc = 0;
        for (i = 0; i < (size + 1); i++)
        {
            crc += data[readInd + 1 + i];
        }
        if (crc != data[readInd + size + 2])
        {
            continue;
        }
        LogPrintf(DEBUG_BLE, "CMD[0x%02X]", data[readInd + 3]);
        /*状态更新*/
        bleInfo->updateTick = sysinfo.sysTick;
        
//        if (bleRelayList[ind].bleInfo.bleLost == 1)
//        {
//            alarmRequestSet(ALARM_BLE_RESTORE_REQUEST);
//            byteToHexString(bleRelayList[ind].addr, debug, 6);
//            debug[12] = 0;
//            LogPrintf(DEBUG_BLE, "^^BLE %s restore", debug);
//        }
//        bleRelayList[ind].bleInfo.bleLost = 0;
        switch (data[readInd + 3])
        {
        	case CMD_DEV_LOGIN_INFO:
        		if (data[readInd + 19])
        		{
					devLoginInfoParser(ind, data + readInd + 4, data[readInd + 19], data + readInd + 20, bleInfo);
				}
				else
				{
					devLoginInfoParser(ind, data + readInd + 4, data[readInd + 19], NULL, bleInfo);
				}
				bleSendDataReqClear(ind, BLE_SEND_LOGININFO_EVENT);
        		break;
 			case CMD_DEV_HEARTBEAT:
				bleInfo->sockData.vol = (float)(((data[readInd + 4] << 8) | data[readInd + 5]) * 0.1);
				bleInfo->sockData.bat = data[readInd + 6];
				bleInfo->sockData.step = (data[readInd + 7] << 8) | data[readInd + 8];
				LogPrintf(DEBUG_BLE, "$$ Ble get dev info==> SN:%s vol:%.1f bat:%d%% step:%d", bleInfo->sockData.SN, bleInfo->sockData.vol, bleInfo->sockData.bat, bleInfo->sockData.step);
				blePetServerUploadUpdate(&bleInfo->sockData);
				/* 添加：开启上报心跳包请求 */
				bleSendDataReqClear(ind, BLE_SEND_HBT_EVNET);
 				break;
 			case CMD_DEV_MASTER_INFO:
 				/* 进入此表示两个主机同时接入从机，其中一个主机被从机拒绝了 */
 				if (data[readInd + 4] == 0)
 				{
 					if (blePetSearchServerSn(bleInfo->sockData.SN) >= 0)
 					{
 						/* 从server列表删除 */
 						blePetServerDel(bleInfo->sockData.SN);
 						/* 从socket列表删除,可以不要 */
						socketDel(blePetSearchServerSn(bleInfo->sockData.SN));
						/* 从blecon列表删除，可加可不加，加的话会一直重连 */
						bleInfo->sockSuccess = 0;
 					}
 					LogPrintf(DEBUG_BLE, "$$ Ble master info fail");
 				}
 				else
 				{
 					bleInfo->sockSuccess = 1;
 					LogPrintf(DEBUG_BLE, "$$ Ble master info success");
 				}
				bleSendDataReqClear(ind, BLE_SEND_MASTERINFO_EVENT);
 				break;
        }
        readInd += size + 3;
    }
}

void bleSendDataReqSet(uint8_t ind, uint32_t req)
{
	deviceConnInfo_s *devinfo;
	devinfo = bleDevGetInfoByIndex(ind);
	if (devinfo->discState == BLE_DISC_STATE_COMP && devinfo->use)
	{
		devinfo->dataEvent |= req;
		LogPrintf(DEBUG_BLE, "$$ bleSendDataReqSet[%d]==>0x%x", ind, req);
	}
	else
	{
		LogPrintf(DEBUG_BLE, "$$ bleSendDataReqSet==>error, use:%d, status:%d", devinfo->use, devinfo->discState);
	}
}

void bleSendDataReqClear(uint8_t ind, uint32_t req)
{
	deviceConnInfo_s *devinfo;
	devinfo = bleDevGetInfoByIndex(ind);
	devinfo->dataEvent &= ~req;
	LogPrintf(DEBUG_BLE, "$$ bleSendDataReqClear[%d]==>0x%x", ind, req);
}

/**************************************************
@bref       蓝牙发送协议
@param
    cmd     指令类型
    data    数据
    data_len数据长度
@return
@note
**************************************************/


static void bleSendProtocol(uint16_t connHandle, uint16_t charHandle, uint8_t cmd, uint8_t *data, uint8_t data_len)
{
    unsigned char i, size_len, lrc;
//    char message[50];
    uint8_t ret;
    char mcu_data[64] = { 0 };
    size_len = 0;
    mcu_data[size_len++] = 0x0c;
    mcu_data[size_len++] = data_len + 1;
    mcu_data[size_len++] = cmd;
    i = 3;
    if (data_len > 0 && data == NULL)
    {
        return;
    }
    while (data_len)
    {
        mcu_data[size_len++] = *data++;
        i++;
        data_len--;
    }
    lrc = 0;
    for (i = 1; i < size_len; i++)
    {
        lrc += mcu_data[i];
    }
    mcu_data[size_len++] = lrc;
    mcu_data[size_len++] = 0x0d;
//    changeHexStringToByteArray((uint8_t *)message, (uint8_t *)mcu_data,size_len);
//    message[size_len * 2] = 0;
    //LogPrintf(DEBUG_BLE, "ble send :%s", message);
    //bleClientSendData(0, 0, (uint8_t *) mcu_data, size_len);
    ret = bleCentralSend(connHandle, charHandle, mcu_data, size_len);

    switch (ret)
    {
        case bleTimeout:
            LogMessage(DEBUG_BLE, "bleTimeout");
            bleCentralDisconnect(connHandle);
            break;
    }
}

void bleProtocolSendEventTask(void)
{
	static uint8_t ind = 0;
	deviceConnInfo_s *devinfo;
	uint8_t param[20] = { 0 };
	

	devinfo = bleDevGetInfoByIndex(ind);
	if (devinfo->discState == BLE_DISC_STATE_COMP && devinfo->dataEvent != 0)
	{
		if (devinfo->dataEvent & BLE_SEND_LOGININFO_EVENT)
		{
			LogMessage(DEBUG_BLE, "try to get pet login info");
			bleSendProtocol(devinfo->connHandle, devinfo->charHandle, CMD_DEV_LOGIN_INFO, param, 0);
			return;
		}
		if (devinfo->dataEvent & BLE_SEND_MASTERINFO_EVENT)
		{
			LogMessage(DEBUG_BLE, "try to send master info");
			param[0] = devinfo->sockSuccess;
			if (devinfo->sockSuccess)
			{
				for (uint8_t i = 0; i < 15; i++)
				{
					param[1 + i] = dynamicParam.SN[i];
				}
				param[16] = 0;
			}
			bleSendProtocol(devinfo->connHandle, devinfo->charHandle, CMD_DEV_MASTER_INFO, param, strlen(param));
			return;
		}
		if (devinfo->dataEvent & BLE_SEND_HBT_EVNET)
		{
			LogMessage(DEBUG_BLE, "try to get pet hbt info");
			bleSendProtocol(devinfo->connHandle, devinfo->charHandle, CMD_DEV_HEARTBEAT, param, 0);
			return;
		}

	}
	ind = (ind + 1) % DEVICE_MAX_CONNECT_COUNT;
}

void bleProtocolSendPeriod(void)
{
	deviceConnInfo_s *devinfo;
	uint8_t ind;
	devinfo = bleDevGetInfoAll();
	for (ind = 0; ind < DEVICE_MAX_CONNECT_COUNT; ind++)
	{
		if (devinfo[ind].discState == BLE_DISC_STATE_COMP)
		{
			devinfo[ind].periodTick++;
			if (devinfo[ind].periodTick >= 45)
			{
				devinfo[ind].periodTick = 0;
				bleSendDataReqSet(ind, BLE_SEND_HBT_EVNET);
				/* 询问是否要登陆 */
				if (devinfo[ind].sockSuccess == 0)
				{
					bleSendDataReqSet(ind, BLE_SEND_LOGININFO_EVENT);
				}
			}
		}
		
	}
}




