/*
 * app_bleRelay.c
 *
 *  Created on: Oct 27, 2022
 *      Author: idea
 */
#include "app_bleRelay.h"
#include "app_central.h"
#include "app_param.h"
#include "app_task.h"
#include "aes.h"
#include "app_port.h"
#include "app_encrypt.h"
#include "app_instructioncmd.h"
#include "app_server.h"

static bleRelayDev_s bleRelayList[BLE_CONNECT_LIST_SIZE];


static void bleRelaySendProtocol(uint16_t connHandle, uint16_t charHandle, uint8_t cmd, uint8_t *data,
                                 uint8_t data_len);


/**************************************************
@bref       为对应蓝牙设备设置数据请求标志
@param
@return
@note
**************************************************/

void bleRelaySetReq(uint8_t ind, uint32_t req)
{
    if (ind >= BLE_CONNECT_LIST_SIZE)
    {
        return;
    }
    bleRelayList[ind].dataReq |= req;
    LogPrintf(DEBUG_BLE, "bleRelaySetReq[%d]:(%04X)", ind, req);
}

/**************************************************
@bref       为所有已使用蓝牙设备设置请求标志
@param
@return
@note
**************************************************/

void bleRelaySetAllReq(uint32_t req)
{
    uint8_t i;
    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (bleRelayList[i].used)
        {
            bleRelaySetReq(i, req);
        }
    }
}

/**************************************************
@bref       为对应蓝牙设备清除数据请求标志
@param
@return
@note
**************************************************/

void bleRelayClearReq(uint8_t ind, uint32_t req)
{
    if (ind >= BLE_CONNECT_LIST_SIZE)
    {
        return;
    }
    bleRelayList[ind].dataReq &= ~req;
    LogPrintf(DEBUG_BLE, "bleRelayClearReq(%d):[%04X]", ind, req);
}

/**************************************************
@bref       为所有已使用蓝牙设备清除请求标志
@param
@return
@note
**************************************************/

void bleRelayClearAllReq(uint32_t req)
{
    uint8_t i;
    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (bleRelayList[i].used)
        {
            bleRelayList[i].dataReq &= ~req;
            bleRelayClearReq(i, req);
        }
    }
}

/**************************************************
@bref       添加新的蓝牙设备到链接列表中
@param
@return
@note
**************************************************/

int8_t bleRelayInsert(uint8_t *addr, uint8_t addrType)
{
    int8_t ret = -1;
    uint8_t i;
    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (bleRelayList[i].used == 0)
        {
            tmos_memset(&bleRelayList[i], 0, sizeof(bleRelayDev_s));
            bleRelayList[i].used = 1;
            tmos_memcpy(bleRelayList[i].addr, addr, 6);
            bleRelayList[i].addrType = addrType;
            ret = bleDevConnAdd(addr, addrType);
//            bleRelaySetReq(i, BLE_EVENT_SET_RF_THRE | BLE_EVENT_SET_OUTV_THRE | BLE_EVENT_SET_AD_THRE | BLE_EVENT_GET_AD_THRE |
//                           BLE_EVENT_GET_RF_THRE | BLE_EVENT_GET_OUT_THRE | BLE_EVENT_GET_OUTV | BLE_EVENT_GET_RFV |
//                           BLE_EVENT_GET_PRE_PARAM | BLE_EVENT_SET_PRE_PARAM | BLE_EVENT_GET_PRE_PARAM | BLE_EVENT_CHK_SOCKET);
            return ret;

        }
    }
    return ret;
}

/**************************************************
@bref       初始化
@param
@return
@note
**************************************************/

void bleRelayInit(void)
{
    uint8_t i;
    uint8_t mac[6];
    tmos_memset(mac, 0, 6);
    tmos_memset(&bleRelayList, 0, sizeof(bleRelayList));
    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (tmos_memcmp(mac, sysparam.bleConnMac[i], 6) == TRUE)
        {
            continue;
        }
        bleRelayInsert(sysparam.bleConnMac[i], 0);
    }
    /* 上电是否要关一次继电器，取决于存不存在关继电器过程中会复位 */
    /* 如果加上下面的逻辑，那么如果发了acc off 那么每次上电都要去连断连继电器       且每次上电没联网之前就会去连接蓝牙*/
//    if (sysparam.relayCtl)
//    {
//        relayAutoRequest();
//    }
}

/**************************************************
@bref       删除所有设备
@param
@return
@note
**************************************************/

void bleRelayDeleteAll(void)
{
    bleDevConnDelAll();
    tmos_memset(bleRelayList, 0, sizeof(bleRelayList));
}

/**************************************************
@bref       获取蓝牙信息
@param
@return
@note
**************************************************/

bleRelayInfo_s *bleRelayGeInfo(uint8_t i)
{
    if (i >= BLE_CONNECT_LIST_SIZE)
    {
        return NULL;
    }
    if (bleRelayList[i].used == 0)
    {
        return NULL;
    }
    return &bleRelayList[i].bleInfo;
}

/**************************************************
@bref       蓝牙无网连接通讯管理
@param
@return
@note	1:开启蓝牙调度			1:关闭蓝牙调度	
可能存在一种情况是蓝牙能够连接，但是一连就断，那么主机会
一直连接，就会导致占用继电器的蓝牙，所以如果出现这种情况
就只连三分钟，超过3分钟直接断了过段时间再连

**************************************************/

//uint8_t bleNoNetHandShakeTask(void)
//{
//
//	static uint8_t connFlag = 0;
//	uint8_t i ,cnt = 0;
//	bleRelayInfo_s *relayinfo;
//	deviceConnInfo_s *devinfo;
//	static uint32_t timeouttick = 0;
//	LogPrintf(DEBUG_ALL, "Cmd:%d, connFlag:%d, timeouttick:%d", sysinfo.bleforceCmd, connFlag, timeouttick);
//	if (primaryServerIsReady() == 0)
//    {
//		for (i = 0; i < bleDevGetCnt(); i++)
//	    {
//	        relayinfo = bleRelayGeInfo(i);
//	        devinfo = bleDevGetInfo(i);
//	        if (relayinfo != NULL)
//	        {
//	        	/* 特殊连接要求 */
//				/* 断连后要以断连参数的1/2的频率去和BLE握          手 */
//				/* 断连参数为0则不进行握手 */
//	        	/* 握手时长不超过3分钟 */ 
//	        	/* 有特殊指令则需要连接蓝牙 */
//	        	if (primaryServerIsReady() == 0)
//	        	{
//					if ((sysparam.bleAutoDisc != 0 &&
//						(sysinfo.sysTick - relayinfo->updateTick) >= sysparam.bleAutoDisc * 60 / 2 &&
//						  sysinfo.sysTick != 0) ||
//						  sysinfo.bleforceCmd != 0)
//					{
//						devinfo->disReq = 0;
//					}
//					
//				}
//				else
//				{
//					devinfo->disReq = 0;
//				}
//				
//	        }
//	    }
//    }
//    /* 有网络，保持蓝牙连接 */
//    else
//    {
//    	connFlag = 0;
//    	timeouttick = 0;
//    	LogMessage(DEBUG_ALL, "111");
//		return 1;
//    }
//    
////    if (connFlag)
////    {
////    	/* 特殊连接要求最多只连3分钟 */
////		if (timeouttick++ >= 180)  
////		{
////			timeouttick = 0;
////			connFlag = 0;
////			sysinfo.bleforceCmd = 0;	// 把强行发蓝牙指令的标志清掉
////			LogMessage(DEBUG_ALL, "222");
////			return 0;
////		}
////		LogMessage(DEBUG_ALL, "333");
////		return 1;
////    }
////    else
////    {
////		timeouttick = 0;
////		LogMessage(DEBUG_ALL, "444");
////		return 0;
////    }
//}



/**************************************************
@bref       蓝牙断连侦测
@param
@return
@note
**************************************************/

static void bleDiscDetector(void)
{
    uint8_t ind;
    uint32_t tick;
    char debug[20];
    if (sysparam.bleAutoDisc == 0)
    {
        return;
    }
    for (ind = 0; ind < BLE_CONNECT_LIST_SIZE; ind++)
    {
        if (bleRelayList[ind].used == 0)
        {
            return;
        }
        if (bleRelayList[ind].bleInfo.bleLost == 1)
        {
            return;
        }
        tick = sysinfo.sysTick - bleRelayList[ind].bleInfo.updateTick;
        if (tick >= (sysparam.bleAutoDisc * 60))
        {
            bleRelayList[ind].bleInfo.bleLost = 1;
            alarmRequestSet(ALARM_BLE_LOST_REQUEST);
            byteToHexString(bleRelayList[ind].addr, debug, 6);
            debug[12] = 0;
            LogPrintf(DEBUG_BLE, "oh ,BLE [%s] lost", debug);
        }
    }
}

/**************************************************
@bref       周期任务
@param
@return
@note
**************************************************/

void blePeriodTask(void)
{
    static uint8_t runTick = 0;
    if (runTick++ >= 30)
    {
        runTick = 0;
        bleRelaySetAllReq(BLE_EVENT_GET_OUTV | BLE_EVENT_GET_RFV | BLE_EVENT_CHK_SOCKET);
    }
    bleDiscDetector();
}

/**************************************************
@bref       数据发送控制
@param
@return
@note
**************************************************/

void bleRelaySendDataTry(void)
{
    static uint8_t ind = 0;
    uint8_t param[20], paramLen;
    uint16_t year;
    uint8_t  month;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t value16;
    uint32_t event;
    deviceConnInfo_s *devInfo;

    ind = ind % BLE_CONNECT_LIST_SIZE;
    for (; ind  < BLE_CONNECT_LIST_SIZE; ind++)
    {
        event = bleRelayList[ind].dataReq;
        if (event != 0 && bleRelayList[ind].used)
        {
            devInfo = bleDevGetInfo(bleRelayList[ind].addr);
            if (devInfo == NULL)
            {
                LogMessage(DEBUG_BLE, "Not find devinfo");
                continue;
            }
            if (devInfo->notifyDone && devInfo->connHandle != INVALID_CONNHANDLE)
            {
                LogPrintf(DEBUG_BLE, "bleRelaySendDataTry(%d),Handle[%d]", ind, devInfo->connHandle);

                if (event & BLE_EVENT_SET_RTC)
                {
                    portGetRtcDateTime(&year, &month, &date, &hour, &minute, &second);
                    param[0] = year % 100;
                    param[1] = month;
                    param[2] = date;
                    param[3] = hour;
                    param[4] = minute;
                    param[5] = second;
                    LogMessage(DEBUG_BLE, "try to update RTC");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_SET_RTC, param, 6);
                    break;
                }

                //固定发送组
                if (event & BLE_EVENT_GET_OUTV)
                {
                    LogMessage(DEBUG_BLE, "try to get outside voltage");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_OUTV, param, 0);
                    break;
                }
                if (event & BLE_EVENT_GET_RFV)
                {
                    LogMessage(DEBUG_BLE, "try to get rf voltage");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_ADCV, param, 0);
                    break;
                }
                if (event & BLE_EVENT_CHK_SOCKET)
                {
					LogPrintf(DEBUG_ALL, "try to send socket status is %s", primaryServerIsReady() ? "OK" : "ERR");
					param[0] = primaryServerIsReady();
					bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_CHK_SOCKET_STATUS, param, 1);
					bleRelayClearReq(ind, BLE_EVENT_CHK_SOCKET);
					break;
                }

                //非固定发送组
                if (event & BLE_EVENT_SET_DEVON)
                {
                    LogMessage(DEBUG_BLE, "try to set relay on");
					createEncrypt(bleRelayList[ind].addr, param, &paramLen);
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_DEV_ON, param, paramLen);
                    break;
                }
                if (event & BLE_EVENT_SET_DEVOFF)
                {
                    LogMessage(DEBUG_BLE, "try to set relay off");
					createEncrypt(bleRelayList[ind].addr, param, &paramLen);
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_DEV_OFF, param, paramLen);
                    break;
                }
                if (event & BLE_EVENT_CLR_CNT)
                {
                    LogMessage(DEBUG_BLE, "try to clear shield");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_CLEAR_SHIELD_CNT, param, 0);
                    break;
                }
                if (event & BLE_EVENT_SET_RF_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to set shield voltage");
                    param[0] = sysparam.bleRfThreshold;
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_SET_VOLTAGE, param, 1);
                    break;
                }
                if (event & BLE_EVENT_SET_OUTV_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to set accoff voltage");
                    value16 = sysparam.bleOutThreshold;
                    param[0] = value16 >> 8 & 0xFF;
                    param[1] = value16 & 0xFF;
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_SET_OUTVOLTAGE, param, 2);
                    break;
                }
                if (event & BLE_EVENT_SET_AD_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to set auto disconnect param");
                    param[0] = sysparam.bleAutoDisc;
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_AUTODIS, param, 1);
                    break;
                }
                if (event & BLE_EVENT_CLR_ALARM)
                {
                    LogMessage(DEBUG_BLE, "try to clear alarm");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_CLEAR_ALARM, param, 1);
                    break;
                }

                if (event & BLE_EVENT_GET_RF_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to get rf threshold");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_VOLTAGE, param, 0);
                    break;
                }

                if (event & BLE_EVENT_GET_OUT_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to get out threshold");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_OUTVOLTAGE, param, 0);
                    break;
                }
                if (event & BLE_EVENT_CLR_PREALARM)
                {
                    LogMessage(DEBUG_BLE, "try to clear prealarm");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_CLEAR_PREALARM, param, 1);
                    break;
                }
                if (event & BLE_EVENT_SET_PRE_PARAM)
                {
                    LogMessage(DEBUG_BLE, "try to set preAlarm param");
                    param[0] = sysparam.blePreShieldVoltage;
                    param[1] = sysparam.blePreShieldDetCnt;
                    param[2] = sysparam.blePreShieldHoldTime;
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_SET_PRE_ALARM_PARAM, param, 3);
                    break;
                }
                if (event & BLE_EVENT_GET_PRE_PARAM)
                {
                    LogMessage(DEBUG_BLE, "try to get preAlarm param");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_PRE_ALARM_PARAM, param, 0);
                    break;
                }
                if (event & BLE_EVENT_GET_AD_THRE)
                {
                    LogMessage(DEBUG_BLE, "try to get auto disc param");
                    bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_GET_DISCONNECT_PARAM, param, 0);
                    break;
                }
                if (event & BLE_EVENT_RES_LOCK_ALRAM)
		        {
					LogMessage(DEBUG_ALL, "try to clear shield lock alarm");
					bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_RES_SHIELD_LOCK_ALRAM, param, 1);
					break;
		        }
		        if (event & BLE_EVENT_CLR_LOCK_ALARM)
			    {
					LogMessage(DEBUG_ALL, "try to clear shield lock");
			        bleRelaySendProtocol(devInfo->connHandle, devInfo->charHandle, CMD_CLEAR_SHIELD_LOCK_ALRAM, param, 0);
			    }
            }

        }
    }
    if (ind != BLE_CONNECT_LIST_SIZE)
    {
        ind++;
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

static void bleRelaySendProtocol(uint16_t connHandle, uint16_t charHandle, uint8_t cmd, uint8_t *data, uint8_t data_len)
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



/**************************************************
@bref       协议解析
@param
    data
    len
@return
@note       0C 04 80 09 04 E7 78 0D
**************************************************/

void bleRelayRecvParser(uint8_t connHandle, uint8_t *data, uint8_t len)
{
    uint8_t readInd, size, crc, i, ind = BLE_CONNECT_LIST_SIZE;
    uint8_t *addr;
    char debug[20];
    uint16_t value16;
    float valuef;
    if (len <= 5)
    {
        return;
    }
    addr = bleDevGetAddrByHandle(connHandle);

    if (addr == NULL)
    {
        return;
    }

    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (tmos_memcmp(addr, bleRelayList[i].addr, 6) == TRUE)
        {
            ind = i;
            break;
        }
    }
    if (ind == BLE_CONNECT_LIST_SIZE)
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
        //LogPrintf(DEBUG_ALL, "CMD[0x%02X]", data[readInd + 3]);
        /*状态更新*/
        bleRelayList[ind].bleInfo.updateTick = sysinfo.sysTick;
        if (bleRelayList[ind].bleInfo.bleLost == 1)
        {
            alarmRequestSet(ALARM_BLE_RESTORE_REQUEST);
            byteToHexString(bleRelayList[ind].addr, debug, 6);
            debug[12] = 0;
            LogPrintf(DEBUG_BLE, "^^BLE %s restore", debug);
        }
        bleRelayList[ind].bleInfo.bleLost = 0;
        switch (data[readInd + 3])
        {
            case CMD_GET_SHIELD_CNT:
                value16 = data[readInd + 4];
                value16 = value16 << 8 | data[readInd + 5];
                LogPrintf(DEBUG_BLE, "^^BLE==>shield occur cnt %d", value16);
                break;
            case CMD_CLEAR_SHIELD_CNT:
                LogMessage(DEBUG_BLE, "^^BLE==>clear success");
                bleRelayClearReq(ind, BLE_EVENT_CLR_CNT);
                break;
            case CMD_DEV_ON:
                if (data[readInd + 4] == 1)
                {
                    LogMessage(DEBUG_BLE, "^^BLE==>relayon success");
                    alarmRequestSet(ALARM_OIL_CUTDOWN_REQUEST);
                    instructionRespone("relayon success");
                }
                else
                {
                    LogMessage(DEBUG_BLE, "^^BLE==>relayon fail");
                    instructionRespone("relayon fail");
                }
                sysinfo.bleforceCmd--;
                bleRelayClearReq(ind, BLE_EVENT_SET_DEVON);
                break;
            case CMD_DEV_OFF:
                if (data[readInd + 4] == 1)
                {
                    LogMessage(DEBUG_BLE, "^^BLE==>relayoff success");
                    alarmRequestSet(ALARM_OIL_RESTORE_REQUEST);
                    instructionRespone("relayoff success");
                }
                else
                {
                    LogMessage(DEBUG_BLE, "^^BLE==>relayoff fail");
                    instructionRespone("relayoff fail");
                }
                sysinfo.bleforceCmd--;
                bleRelayClearReq(ind, BLE_EVENT_SET_DEVOFF);
                break;
            case CMD_SET_VOLTAGE:
                LogMessage(DEBUG_BLE, "^^BLE==>set shiled voltage success");
                bleRelayClearReq(ind, BLE_EVENT_SET_RF_THRE);
                break;
            case CMD_GET_VOLTAGE:

                valuef = data[readInd + 4] / 100.0;
                bleRelayList[ind].bleInfo.rf_threshold = valuef;
                LogPrintf(DEBUG_BLE, "^^BLE==>get shield voltage range %.2fV", valuef);
                bleRelayClearReq(ind, BLE_EVENT_GET_RF_THRE);
                break;
            case CMD_GET_ADCV:
                value16 = data[readInd + 4];
                value16 = value16 << 8 | data[readInd + 5];
                valuef = value16 / 100.0;
                bleRelayList[ind].bleInfo.rfV = valuef;
                LogPrintf(DEBUG_BLE, "^^BLE==>shield voltage %.2fV", valuef);
                bleRelayClearReq(ind, BLE_EVENT_GET_RFV);
                break;
            case CMD_SET_OUTVOLTAGE:
                LogMessage(DEBUG_BLE, "^^BLE==>set acc voltage success");
                bleRelayClearReq(ind, BLE_EVENT_SET_OUTV_THRE);
                break;
            case CMD_GET_OUTVOLTAGE:
                value16 = data[readInd + 4];
                value16 = value16 << 8 | data[readInd + 5];
                valuef = value16 / 100.0;
                bleRelayList[ind].bleInfo.out_threshold = valuef;
                LogPrintf(DEBUG_BLE, "^^BLE==>get outside voltage range %.2fV", valuef);
                bleRelayClearReq(ind, BLE_EVENT_GET_OUT_THRE);
                break;
            case CMD_GET_OUTV:
                value16 = data[readInd + 4];
                value16 = value16 << 8 | data[readInd + 5];
                valuef = value16 / 100.0;
                bleRelayList[ind].bleInfo.outV = valuef;
                LogPrintf(DEBUG_BLE, "^^BLE==>outside voltage %.2fV", valuef);
                bleRelayClearReq(ind, BLE_EVENT_GET_OUTV);
                break;
            case CMD_GET_ONOFF:
                LogPrintf(DEBUG_BLE, "^^BLE==>relay state %d", data[readInd + 4]);
                break;
            case CMD_ALARM:
//            	sysparam.relayCtl = 1;
//            	paramSaveAll();
//            	relayAutoRequest();
                LogMessage(DEBUG_BLE, "^^BLE==>shield alarm occur");
                LogMessage(DEBUG_BLE, "^^oh, 蓝牙屏蔽报警...");
                alarmRequestSet(ALARM_SHIELD_REQUEST);
                bleRelaySetReq(ind, BLE_EVENT_CLR_ALARM);
                break;
            case CMD_AUTODIS:
                LogMessage(DEBUG_BLE, "^^BLE==>set auto disconnect success");
                bleRelayClearReq(ind, BLE_EVENT_SET_AD_THRE);
                break;
            case CMD_CLEAR_ALARM:
                LogMessage(DEBUG_BLE, "^^BLE==>clear alarm success");
                bleRelayClearReq(ind, BLE_EVENT_CLR_ALARM);
                break;
            case CMD_PREALARM:
                LogMessage(DEBUG_BLE, "^^BLE==>preshield alarm occur");
                LogMessage(DEBUG_BLE, "^^oh, 蓝牙预警...");
                alarmRequestSet(ALARM_PREWARN_REQUEST);
                bleRelaySetReq(ind, BLE_EVENT_CLR_PREALARM);
                break;
            case CMD_CLEAR_PREALARM:
                LogMessage(DEBUG_BLE, "^^BLE==>clear prealarm success");
                bleRelayClearReq(ind, BLE_EVENT_CLR_PREALARM);
                break;
            case CMD_SET_PRE_ALARM_PARAM:
                LogMessage(DEBUG_BLE, "^^BLE==>set prealarm param success");
                bleRelayClearReq(ind, BLE_EVENT_SET_PRE_PARAM);
                break;
            case CMD_GET_PRE_ALARM_PARAM:
                LogPrintf(DEBUG_BLE, "^^BLE==>get prealarm param [%d,%d,%d] success", data[readInd + 4], data[readInd + 5],
                          data[readInd + 6]);
                bleRelayList[ind].bleInfo.preV_threshold = data[readInd + 4];
                bleRelayList[ind].bleInfo.preDetCnt_threshold = data[readInd + 5];
                bleRelayList[ind].bleInfo.preHold_threshold = data[readInd + 6];
                bleRelayClearReq(ind, BLE_EVENT_GET_PRE_PARAM);
                break;
            case CMD_GET_DISCONNECT_PARAM:
                LogPrintf(DEBUG_BLE, "^^BLE==>auto disc %d minutes", data[readInd + 4]);
                bleRelayList[ind].bleInfo.disc_threshold = data[readInd + 4];
                bleRelayClearReq(ind, BLE_EVENT_GET_AD_THRE);
                break;
            case CMD_SET_RTC:
                LogMessage(DEBUG_BLE, "^^BLE==>RTC update success");
                bleRelayClearReq(ind, BLE_EVENT_SET_RTC);
                break;
            case CMD_CHK_SOCKET_STATUS:
				LogPrintf(DEBUG_ALL, "BLE==>check socket status");
            	break;
           	case CMD_SEND_SHIELD_LOCK_ALARM:
           	    sysparam.relayCtl = 1;
            	paramSaveAll();
            	relayAutoRequest();
				alarmRequestSet(ALARM_SHIELD_REQUEST);
				bleRelaySetReq(ind, BLE_EVENT_RES_LOCK_ALRAM);
           		break;
           	case CMD_RES_SHIELD_LOCK_ALRAM:
				LogMessage(DEBUG_ALL, "BLE==>res alarm success");
				bleRelayClearReq(ind, BLE_EVENT_RES_LOCK_ALRAM);
            	break;
            case CMD_CLEAR_SHIELD_LOCK_ALRAM:
				LogMessage(DEBUG_ALL, "BLE==>clear shield lock success");
                bleRelayClearReq(ind, BLE_EVENT_CLR_LOCK_ALARM);
           		break;
        }
        readInd += size + 3;
    }
}

