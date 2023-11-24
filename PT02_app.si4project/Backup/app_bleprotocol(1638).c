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
@bref      	��¼��Ϣ����
@param
	ind			�����豸��Ϣָ��
    sn			��¼sn��
    connSuccess	�Ƿ��ѵ�¼
    masterSn	pt13����sn��
    bleinfo     ������Ϣ
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
	/* �Ѵ������������� */
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
	/* �豸������ */
	if (connSuccess == 1)
	{
		/* ���ж�����sn���Ƿ����Լ� */
		if (strncmp(master, dynamicParam.SN, 15) == 0)
		{
			memcpy(bleinfo->sockData.SN, slavor, 15);
			bleinfo->sockData.SN[15] = 0;
			bleinfo->sockSuccess = 1;
			bleinfo->socketId = blePetServerAdd(bleinfo->sockData.SN);
			bleSendDataReqSet(ind, BLE_SEND_MASTERINFO_EVENT);
			LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Local link, LocolSn[%s] MasterSn[%s] SlavorSn:[%s]", dynamicParam.SN, master, bleinfo->sockData.SN);
		}
		/* �����״̬��ʾ�豸��ͨ�����PT02���� */
		else
		{
			/* ��server�б�ɾ�� */
			blePetServerDel(slavor);
			/* ��socket�б�ɾ��,���Բ�Ҫ */
			socketDel(blePetSearchServerSn(slavor));
			/* ��blecon�б�ɾ�����ɼӿɲ��ӣ��ӵĻ���һֱ���� */

			LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Other link, LocolSn[%s] MasterSn:[%s] SlavorSn:[%s]", dynamicParam.SN, master, slavor);
 			
		}
		return;
	}
	/* δ�����ұ��������ڸ�sn����·����ѯ������· */
	if (blePetSearchIdleServer() < 0)
	{
		LogPrintf(DEBUG_BLE, "$$ Ble get login info==>No idle server");
		return;
	}
	/* �������� */
	memcpy(bleinfo->sockData.SN, slavor, 15);
	bleinfo->sockData.SN[15] = 0;
	bleinfo->sockSuccess = 1;
	bleinfo->socketId = blePetServerAdd(bleinfo->sockData.SN);
	bleSendDataReqSet(ind, BLE_SEND_MASTERINFO_EVENT);
	/* ȷ�������Ƿ�Ҫɾ����· */
	socketDel(bleinfo->socketId);
	LogPrintf(DEBUG_BLE, "$$ Ble get login info==>Add ble server, Sn:[%s] Socketid:%d", bleinfo->sockData.SN, bleinfo->socketId);
	
}

/**************************************************
@bref      	�ϱ���Ϣ¼��
@param
    sn			��¼sn��
    sockFlag	�Ƿ��ѵ�¼
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
@bref       ����Э�����
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
            //���ݳ�����
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
        /*״̬����*/
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
				/* ��ӣ������ϱ����������� */
				bleSendDataReqClear(ind, BLE_SEND_HBT_EVNET);
 				break;
 			case CMD_DEV_MASTER_INFO:
 				/* ����˱�ʾ��������ͬʱ����ӻ�������һ���������ӻ��ܾ��� */
 				if (data[readInd + 4] == 0)
 				{
 					if (blePetSearchServerSn(bleInfo->sockData.SN) >= 0)
 					{
 						/* ��server�б�ɾ�� */
 						blePetServerDel(bleInfo->sockData.SN);
 						/* ��socket�б�ɾ��,���Բ�Ҫ */
						socketDel(blePetSearchServerSn(bleInfo->sockData.SN));
						/* ��blecon�б�ɾ�����ɼӿɲ��ӣ��ӵĻ���һֱ���� */
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
	if (devinfo->discState == BLE_DISC_STATE_COMP && devinfo->use)
	{
		devinfo->dataEvent &= ~req;
		LogPrintf(DEBUG_BLE, "$$ bleSendDataReqClear[%d]==>0x%x", ind, req);
	}
	else
	{
		LogPrintf(DEBUG_BLE, "$$ bleSendDataReqClear==>error, use:%d, status:%d", devinfo->use, devinfo->discState);
	}
}

/**************************************************
@bref       ��������Э��
@param
    cmd     ָ������
    data    ����
    data_len���ݳ���
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
		if (devinfo->dataEvent & BLE_SEND_HBT_EVNET)
		{
			LogMessage(DEBUG_BLE, "try to get pet hbt info");
			bleSendProtocol(devinfo->connHandle, devinfo->charHandle, CMD_DEV_HEARTBEAT, param, 0);
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
				/* ѯ���Ƿ�Ҫ��½ */
				if (devinfo[ind].sockSuccess == 0)
				{
					bleSendDataReqSet(ind, BLE_SEND_LOGININFO_EVENT);
				}
			}
		}
		
	}
}




