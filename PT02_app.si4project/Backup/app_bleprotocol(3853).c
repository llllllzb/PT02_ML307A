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
    uint8_t readInd, size, crc, i, ind = BLE_CONNECT_LIST_SIZE;
    uint8_t *addr;
    char debug[20];
    uint16_t value16;
    float valuef;
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
 				break;
 			case CMD_DEV_HEARTBEAT:

 				break;

        }
        readInd += size + 3;
    }
}



