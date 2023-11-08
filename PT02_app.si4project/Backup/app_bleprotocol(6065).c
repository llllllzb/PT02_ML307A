/*
 * app_bleprotocol.c
 *
 *  Created on: Oct 28, 2023
 *      Author: nimo
 */

#include "app_bleprotocol.h"
#include "app_central.h"


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
    char debug[20];
    uint16_t value16;
    float valuef;
    int8_t ind;
    deviceConnInfo_s *bleInfo;
    if (len <= 5)
    {
        return;
    }
    bleInfo = bleDevGetInfoByHandle(connHandle);
    if (bleInfo == NULL)
    {
        return;
    }
    ind = bleDevGetIndexByHandle(connHandle);
	if (ind == -1)
	{
		LogPrintf(DEBUG_BLE, "$$ Ble list can not find this device");
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
 			case CMD_DEV_INFO_PARAM:
				memcpy(bleInfo->sockData.SN, data + readInd + 4, 15);
				bleInfo->sockData.SN[15] = 0;
				bleInfo->sockData.vol = (float)((data[readInd + 5] << 8) | data[readInd + 6]) * 0.1;
				bleInfo->sockData.bat = data[readInd + 7];
				bleInfo->sockData.step = (data[readInd + 8] << 8) | data[readInd + 9];
				LogPrintf(DEBUG_BLE, "$$ Ble get dev info==> SN:%s vol:%.1f bat:%d%% step:%d", bleInfo->sockData.SN, bleInfo->sockData.vol, bleInfo->sockData.bat, bleInfo->sockData.step);
				bleSendDataReqClear(ind , CMD_DEV_INFO_PARAM);
 				break;
 			case CMD_DEV_HEARTBEAT:
				
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
		LogPrintf(DEBUG_ALL, "$$ bleSendDataReqSet[%d]==>0x%x", ind, req);
	}
	else
	{
		LogPrintf(DEBUG_ALL, "$$ bleSendDataReqSet==>error, use:%d, status:%d", devinfo->use, devinfo->discState);
	}
}

void bleSendDataReqClear(uint8_t ind, uint32_t req)
{
	deviceConnInfo_s *devinfo;
	devinfo = bleDevGetInfoByIndex(ind);
	if (devinfo->discState == BLE_DISC_STATE_COMP && devinfo->use)
	{
		devinfo->dataEvent &= ~req;
		LogPrintf(DEBUG_ALL, "$$ bleSendDataReqClear[%d]==>0x%x", ind, req);
	}
	else
	{
		LogPrintf(DEBUG_ALL, "$$ bleSendDataReqClear==>error, use:%d, status:%d", devinfo->use, devinfo->discState);
	}
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
    //char message[50];
    uint8_t ret;
    char mcu_data[32];
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
    //changeByteArrayToHexString((uint8_t *)mcu_data, (uint8_t *)message, size_len);
    //message[size_len * 2] = 0;
    //LogPrintf(DEBUG_ALL, "ble send :%s", message);
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
	uint8_t param[20];
	
	for (; ind < DEVICE_MAX_CONNECT_COUNT; )
	{
		devinfo = bleDevGetInfoByIndex(ind);
		if (devinfo->discState == BLE_DISC_STATE_COMP && devinfo->dataEvent != 0)
		{
			if (devinfo->dataEvent & CMD_DEV_INFO_PARAM)
			{
				LogMessage(DEBUG_ALL, "try to get pet info");
				bleSendProtocol(devinfo->connHandle, devinfo->charHandle, CMD_DEV_INFO_PARAM, param, 0);
				break;
			}
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
			if (devinfo[ind].periodTick >= 25)
			{
				devinfo[ind].periodTick = 0;
				bleSendDataReqSet(ind, CMD_DEV_INFO_PARAM);
			}
		}
	}
}


