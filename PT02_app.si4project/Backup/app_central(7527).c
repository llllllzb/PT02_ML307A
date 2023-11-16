#include "app_central.h"
#include "app_sys.h"

#include "app_task.h"
#include "app_server.h"
#include "app_param.h"
#include "app_bleprotocol.h"
//全局变量

tmosTaskID bleCentralTaskId = INVALID_TASK_ID;
static gapBondCBs_t bleBondCallBack;
static gapCentralRoleCB_t bleRoleCallBack;
static deviceConnInfo_s devInfoList[DEVICE_MAX_CONNECT_COUNT];
static bleScheduleInfo_s bleSchedule;
static deviceScanList_s scanList;


//函数声明
static tmosEvents bleCentralTaskEventProcess(tmosTaskID taskID, tmosEvents event);
static void bleCentralEventCallBack(gapRoleEvent_t *pEvent);
static void bleCentralHciChangeCallBack(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets);
static void bleCentralRssiCallBack(uint16_t connHandle, int8_t newRSSI);
static void bleDevConnInit(void);

static void bleDevConnSuccess(uint8_t *addr, uint16_t connHandle);
static void bleDevDisconnect(uint16_t connHandle);
static void bleDevSetCharHandle(uint16_t connHandle, uint16_t handle);
static void bleDevSetNotifyHandle(uint16_t connHandle, uint16_t handle);

static void bleDevSetServiceHandle(uint16_t connHandle, uint16_t findS, uint16_t findE);
static void bleDevDiscoverAllServices(void);
static void bleDevDiscoverAllChars(uint16_t connHandle);
static void bleDevDiscoverNotify(uint16_t connHandle);
static uint8_t bleDevEnableNotify(void);
static uint8_t bleDevSendDataTest(void);

static void bleSchduleChangeFsm(bleFsm nfsm);
static void bleScheduleTask(void);
static void bleDevDiscoverCharByUuid(void);
static void bleDevDiscoverServByUuid(void);
static void bleScanFsmChange(uint8_t fsm);



/**************************************************
@bref       BLE主机初始化
@param
@return
@note
**************************************************/

void bleCentralInit(void)
{
    bleDevConnInit();
    
    //bleRelayInit();
    GAPRole_CentralInit();
    GAP_SetParamValue(TGAP_DISC_SCAN, 12800);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, 20);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, 100);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, 100);

    bleCentralTaskId = TMOS_ProcessEventRegister(bleCentralTaskEventProcess);
    GATT_InitClient();
    GATT_RegisterForInd(bleCentralTaskId);

    bleBondCallBack.pairStateCB = NULL;
    bleBondCallBack.passcodeCB = NULL;

    bleRoleCallBack.eventCB = bleCentralEventCallBack;
    bleRoleCallBack.ChangCB = bleCentralHciChangeCallBack;
    bleRoleCallBack.rssiCB = bleCentralRssiCallBack;

    tmos_set_event(bleCentralTaskId, BLE_TASK_START_EVENT);
    tmos_start_reload_task(bleCentralTaskId, BLE_TASK_SCHEDULE_EVENT, MS1_TO_SYSTEM_TIME(1000));
}


/**************************************************
@bref      ATT_FIND_BY_TYPE_VALUE_RSP回调
@param
@return
@note
**************************************************/
static void attFindByTypeValueRspCB(gattMsgEvent_t *pMsgEvt)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use && devInfoList[i].discState == BLE_DISC_STATE_SVC)
		{
			if (pMsgEvt->msg.findByTypeValueRsp.numInfo > 0)
			{
				bleDevSetServiceHandle(pMsgEvt->connHandle, \
					ATT_ATTR_HANDLE(pMsgEvt->msg.findByTypeValueRsp.pHandlesInfo, 0), \
					ATT_GRP_END_HANDLE(pMsgEvt->msg.findByTypeValueRsp.pHandlesInfo, 0));
			}
			if (pMsgEvt->hdr.status == bleProcedureComplete || pMsgEvt->hdr.status == bleTimeout)
	        {	
				bleDevDiscoverCharByUuid();
	        }
		}
	}	
}

/**************************************************
@bref	   ATT_READ_BY_TYPE_RSP回调
@param
@return
@note
**************************************************/
static void attReadByTypeRspCB(gattMsgEvent_t *pMsgEvt)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use && devInfoList[i].discState == BLE_DISC_STATE_CHAR
								&& devInfoList[i].findServiceDone == 1)
		{
			if (pMsgEvt->msg.readByTypeRsp.numPairs > 0)
			{
                bleDevSetCharHandle(devInfoList[i].connHandle, BUILD_UINT16(pMsgEvt->msg.readByTypeRsp.pDataList[0],
                                         pMsgEvt->msg.readByTypeRsp.pDataList[1]));
			}
			if ((pMsgEvt->method == ATT_READ_BY_TYPE_RSP && pMsgEvt->hdr.status == bleProcedureComplete))
            {
				bleDevDiscoverNotify(devInfoList[i].connHandle);
            }
		}
		if (devInfoList[i].use && devInfoList[i].discState == BLE_DISC_STATE_CCCD
								&& devInfoList[i].findCharDone == 1)
		{
			if (pMsgEvt->msg.readByTypeRsp.numPairs > 0)
			{
				bleDevSetNotifyHandle(devInfoList[i].connHandle, BUILD_UINT16(pMsgEvt->msg.readByTypeRsp.pDataList[0],
                                                                 pMsgEvt->msg.readByTypeRsp.pDataList[1]));
               	tmos_start_task(bleCentralTaskId, BLE_TASK_NOTIFYEN_EVENT, MS1_TO_SYSTEM_TIME(1000));
			}
			
		}
	}
}

/**************************************************
@bref       GATT系统消息事件处理
@param
@return
@note
**************************************************/

static void gattMessageHandler(gattMsgEvent_t *pMsgEvt)
{
    char debug[100];
    uint8 dataLen, infoLen, numOf, i;
    uint8 *pData;
    uint8_t uuid16[16];
    uint16 uuid, startHandle, endHandle, findHandle;
    bStatus_t status;
    LogPrintf(DEBUG_ALL, "Handle[%d],Method[0x%02X],Status[0x%02X]", pMsgEvt->connHandle, pMsgEvt->method,
              pMsgEvt->hdr.status);
    switch (pMsgEvt->method)
    {
        case ATT_ERROR_RSP:
            LogPrintf(DEBUG_BLE, "Error,Handle[%d],ReqOpcode[0x%02X],ErrCode[0x%02X]", pMsgEvt->msg.errorRsp.handle,
                      pMsgEvt->msg.errorRsp.reqOpcode, pMsgEvt->msg.errorRsp.errCode);
            break;
        //查找服务 BY UUID
        case ATT_FIND_BY_TYPE_VALUE_RSP:
			attFindByTypeValueRspCB(pMsgEvt);
            break;
        //查找服务 ALL
        case ATT_READ_BY_GRP_TYPE_RSP:
            infoLen = pMsgEvt->msg.readByGrpTypeRsp.len;
            numOf = pMsgEvt->msg.readByGrpTypeRsp.numGrps;
            pData = pMsgEvt->msg.readByGrpTypeRsp.pDataList;
            dataLen = infoLen * numOf;
            if (infoLen != 0)
            {
                byteArrayInvert(pData, dataLen);
                for (i = 0; i < numOf; i++)
                {
                    uuid = 0;
                    switch (infoLen)
                    {
                        case 6:
                            uuid = pData[6 * i];
                            uuid <<= 8;
                            uuid |= pData[6 * i + 1];

                            endHandle = pData[6 * i + 2];
                            endHandle <<= 8;
                            endHandle |= pData[6 * i + 3];

                            startHandle = pData[6 * i + 4];
                            startHandle <<= 8;
                            startHandle |= pData[6 * i + 5];

                            LogPrintf(DEBUG_BLE, "ServUUID: [%04X],Start:0x%04X,End:0x%04X", uuid, startHandle, endHandle);
                            break;
                        case 20:
                            memcpy(uuid16, pData + (20 * i), 16);
                            endHandle = pData[20 * i + 16];
                            endHandle <<= 8;
                            endHandle |= pData[20 * i + 17];

                            startHandle = pData[20 * i + 18];
                            startHandle <<= 8;
                            startHandle |= pData[20 * i + 19];
                            byteToHexString(uuid16, debug, 16);
                            debug[32] = 0;
                            LogPrintf(DEBUG_BLE, "ServUUID: [%s],Start:0x%04X,End:0x%04X", debug, startHandle, endHandle);
                            break;
                    }
                    if (uuid == SERVICE_UUID)
                    {
                        LogPrintf(DEBUG_BLE, "Find my services uuid [%04X]", uuid);
                        bleDevSetServiceHandle(pMsgEvt->connHandle, startHandle, endHandle);
                    }
                }

            }
            if (pMsgEvt->hdr.status == bleProcedureComplete || pMsgEvt->hdr.status == bleTimeout)
            {
                LogPrintf(DEBUG_BLE, "Discover all services done!");
                bleDevDiscoverAllChars(pMsgEvt->connHandle);
            }
            break;
        //查找特征
        case ATT_READ_BY_TYPE_RSP:
			attReadByTypeRspCB(pMsgEvt);
            break;
        //数据发送回复
        case ATT_WRITE_RSP:
            LogPrintf(DEBUG_BLE, "Handle[%d] send %s!", pMsgEvt->connHandle, pMsgEvt->hdr.status == SUCCESS ? "success" : "fail");
            break;
        //收到notify数据
        case ATT_HANDLE_VALUE_NOTI:
            pData = pMsgEvt->msg.handleValueNoti.pValue;
            dataLen = pMsgEvt->msg.handleValueNoti.len;
            byteToHexString(pData, debug, dataLen);
            debug[dataLen * 2] = 0;
            LogPrintf(DEBUG_BLE, "^^Handle[%d],Recv:[%s]", pMsgEvt->connHandle, debug);
            if (pData[0] != 0) {
				
            }
            bleProtoclRecvParser(pMsgEvt->connHandle, pData, dataLen);
            break;
        default:
            LogPrintf(DEBUG_BLE, "It is unprocessed!!!");
            break;
    }
    GATT_bm_free(&pMsgEvt->msg, pMsgEvt->method);
}


/**************************************************
@bref       系统消息事件处理
@param
@return
@note
**************************************************/

static void sysMessageHandler(tmos_event_hdr_t *pMsg)
{
    switch (pMsg->event)
    {
        case GATT_MSG_EVENT:
            gattMessageHandler((gattMsgEvent_t *)pMsg);
            break;
        default:
            LogPrintf(DEBUG_BLE, "Unprocessed Event 0x%02X", pMsg->event);
            break;
    }
}


/**************************************************
@bref       主机任务事件处理
@param
@return
@note
**************************************************/

static tmosEvents bleCentralTaskEventProcess(tmosTaskID taskID, tmosEvents event)
{
    uint8_t *pMsg;
    bStatus_t status;
    if (event & SYS_EVENT_MSG)
    {
        if ((pMsg = tmos_msg_receive(bleCentralTaskId)) != NULL)
        {
            sysMessageHandler((tmos_event_hdr_t *) pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return (event ^ SYS_EVENT_MSG);
    }
    if (event & BLE_TASK_START_EVENT)
    {
        status = GAPRole_CentralStartDevice(bleCentralTaskId, &bleBondCallBack, &bleRoleCallBack);
        if (status == SUCCESS)
        {
			LogMessage(DEBUG_BLE, "master role init..");
        }
        else 
        {
			LogPrintf(DEBUG_BLE, "master role init error, ret:0x%02x", status);
        }
        return event ^ BLE_TASK_START_EVENT;
    }
    if (event & BLE_TASK_SENDTEST_EVENT)
    {
        status = bleDevSendDataTest();
        if (status == blePending)
        {
            LogMessage(DEBUG_BLE, "send pending...");
            tmos_start_task(bleCentralTaskId, BLE_TASK_SENDTEST_EVENT, MS1_TO_SYSTEM_TIME(100));
        }
        return event ^ BLE_TASK_SENDTEST_EVENT;
    }
    if (event & BLE_TASK_NOTIFYEN_EVENT)
    {
        if (bleDevEnableNotify() == SUCCESS)
        {
            if (sysinfo.logLevel == 4)
            {
                LogMessage(DEBUG_FACTORY, "+FMPC:BLE CONNECT SUCCESS");
            }
            LogMessage(DEBUG_BLE, "Notify done!");
            bleSchduleChangeFsm(BLE_SCHEDULE_DONE);
        }
        return event ^ BLE_TASK_NOTIFYEN_EVENT;
    }
	if (event & BLE_TASK_SVC_DISCOVERY_EVENT)
	{
		bleDevDiscoverServByUuid();
		return event ^ BLE_TASK_SVC_DISCOVERY_EVENT;
	}
    if (event & BLE_TASK_SCHEDULE_EVENT)
    {
        bleScheduleTask();
        //blePeriodTask();
        //bleRelaySendDataTry();
        bleProtocolSendEventTask();
        return event ^ BLE_TASK_SCHEDULE_EVENT;
    }
    return 0;
}
/**-----------------------------------------------------------------**/
/**-----------------------------------------------------------------**/
/**************************************************
@bref       处理扫描时扫到的蓝牙设备
@param
@return
@note
**************************************************/

static void deviceInfoEventHandler(gapDeviceInfoEvent_t *pEvent)
{
    uint8 i, dataLen, cmd;
    char debug[100];
    deviceScanInfo_s scaninfo;

    byteToHexString(pEvent->addr, debug, B_ADDR_LEN);
    debug[B_ADDR_LEN * 2] = 0;
    LogPrintf(DEBUG_BLE, "MAC:[%s],TYPE:0x%02X,RSSI:%d", debug, pEvent->addrType, pEvent->rssi);

    tmos_memset(&scaninfo, 0, sizeof(deviceScanInfo_s));
    tmos_memcpy(scaninfo.addr, pEvent->addr, B_ADDR_LEN);
    scaninfo.rssi = pEvent->rssi;
    scaninfo.addrType = pEvent->addrType;
    scaninfo.eventType = pEvent->eventType;


    if (pEvent->pEvtData != NULL && pEvent->dataLen != 0)
    {

        byteToHexString(pEvent->pEvtData, debug, pEvent->dataLen);
        debug[pEvent->dataLen * 2] = 0;
        //LogPrintf(DEBUG_ALL, "BroadCast:[%s]", debug);

        for (i = 0; i < pEvent->dataLen; i++)
        {
            dataLen = pEvent->pEvtData[i];
            if ((dataLen + i + 1) > pEvent->dataLen)
            {
                return ;
            }
            cmd = pEvent->pEvtData[i + 1];
            switch (cmd)
            {
                case GAP_ADTYPE_LOCAL_NAME_SHORT:
                case GAP_ADTYPE_LOCAL_NAME_COMPLETE:
                    if (dataLen > 30)
                    {
                        break;
                    }
                    tmos_memcpy(scaninfo.broadcaseName, pEvent->pEvtData + i + 2, dataLen - 1);
                    scaninfo.broadcaseName[dataLen - 1] = 0;
                    LogPrintf(DEBUG_BLE, "<---->BroadName:[%s]", scaninfo.broadcaseName);
                    if (my_strpach(scaninfo.broadcaseName, "PT13"))
                    {
						bleDevScanInfoAdd(&scaninfo);
                    }
                    break;
                default:
                    //LogPrintf(DEBUG_ALL, "UnsupportCmd:0x%02X", cmd);
                    break;
            }

            i += dataLen;
        }
    }

}
/**************************************************
@bref       与从机建立链接成功
@param
@return
@note
**************************************************/

void linkEstablishedEventHandler(gapEstLinkReqEvent_t *pEvent)
{
    char debug[20];

    if (pEvent->hdr.status != SUCCESS)
    {
        LogPrintf(DEBUG_BLE, "Link established error,Status:[0x%X]", pEvent->hdr.status);
        return;
    }
    byteToHexString(pEvent->devAddr, debug, 6);
    debug[12] = 0;
    LogPrintf(DEBUG_BLE, "Device [%s] connect success", debug);
    bleDevConnSuccess(pEvent->devAddr, pEvent->connectionHandle);
    //bleDevDiscoverAllServices();
}

/**************************************************
@bref       与从机断开链接
@param
@return
@note
**************************************************/

void linkTerminatedEventHandler(gapTerminateLinkEvent_t *pEvent)
{
    LogPrintf(DEBUG_BLE, "Device disconnect,Handle [%d],Reason [0x%02X]", pEvent->connectionHandle, pEvent->reason);
    bleDevDisconnect(pEvent->connectionHandle);
}

/**************************************************
@bref		扫描完成回调
@param
@return
@note
**************************************************/

void gapDeviceDiscoveryEvent(deviceScanList_s *list)
{
    uint8_t i;
    bleInfo_s devInfo;
    for (i = 0; i < list->cnt; i++)
    {
        if (my_strpach(list->list[i].broadcaseName, "PT13"))
        {
            LogPrintf(DEBUG_BLE, "Find Ble [%s],rssi:%d", list->list[i].broadcaseName, list->list[i].rssi);
            bleDevConnAdd(list->list[i].addr, list->list[i].addrType);
            bleScanFsmChange(BLE_SCAN_WAIT);
            return;
        }
    }
    LogMessage(DEBUG_BLE, "no find my ble");
    bleScanFsmChange(BLE_SCAN_IDLE);
}

/**-----------------------------------------------------------------**/
/**-----------------------------------------------------------------**/
/**************************************************
@bref       主机底层事件回调
@param
@return
@note
**************************************************/

static void bleCentralEventCallBack(gapRoleEvent_t *pEvent)
{
    LogPrintf(DEBUG_BLE, "bleCentral Event==>[0x%02X]", pEvent->gap.opcode);
    switch (pEvent->gap.opcode)
    {
        case GAP_DEVICE_INIT_DONE_EVENT:
            LogPrintf(DEBUG_BLE, "bleCentral init done!");
            break;
        case GAP_DEVICE_DISCOVERY_EVENT:
            LogPrintf(DEBUG_BLE, "bleCentral discovery done!");
            gapDeviceDiscoveryEvent(&scanList);
            break;
        case GAP_ADV_DATA_UPDATE_DONE_EVENT:
            break;
        case GAP_MAKE_DISCOVERABLE_DONE_EVENT:
            break;
        case GAP_END_DISCOVERABLE_DONE_EVENT:
            break;
        case GAP_LINK_ESTABLISHED_EVENT:
            linkEstablishedEventHandler(&pEvent->linkCmpl);
            break;
        case GAP_LINK_TERMINATED_EVENT:
            linkTerminatedEventHandler(&pEvent->linkTerminate);
            break;
        case GAP_LINK_PARAM_UPDATE_EVENT:
            break;
        case GAP_RANDOM_ADDR_CHANGED_EVENT:
            break;
        case GAP_SIGNATURE_UPDATED_EVENT:
            break;
        case GAP_AUTHENTICATION_COMPLETE_EVENT:
            break;
        case GAP_PASSKEY_NEEDED_EVENT:
            break;
        case GAP_SLAVE_REQUESTED_SECURITY_EVENT:
            break;
        case GAP_DEVICE_INFO_EVENT:
            deviceInfoEventHandler(&pEvent->deviceInfo);
            break;
        case GAP_BOND_COMPLETE_EVENT:
            break;
        case GAP_PAIRING_REQ_EVENT:
            break;
        case GAP_DIRECT_DEVICE_INFO_EVENT:
            break;
        case GAP_PHY_UPDATE_EVENT:
            break;
        case GAP_EXT_ADV_DEVICE_INFO_EVENT:
            break;
        case GAP_MAKE_PERIODIC_ADV_DONE_EVENT:
            break;
        case GAP_END_PERIODIC_ADV_DONE_EVENT:
            break;
        case GAP_SYNC_ESTABLISHED_EVENT:
            break;
        case GAP_PERIODIC_ADV_DEVICE_INFO_EVENT:
            break;
        case GAP_SYNC_LOST_EVENT:
            break;
        case GAP_SCAN_REQUEST_EVENT:
            break;
    }
}
/**************************************************
@bref       MTU改变时系统回调
@param
@return
@note
**************************************************/

static void bleCentralHciChangeCallBack(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets)
{
    LogPrintf(DEBUG_BLE, "Handle[%d] MTU change ,TX:%d , RX:%d", connHandle, maxTxOctets, maxRxOctets);
}
/**************************************************
@bref       主动读取rssi回调
@param
@return
@note
**************************************************/

static void bleCentralRssiCallBack(uint16_t connHandle, int8_t newRSSI)
{
    LogPrintf(DEBUG_BLE, "Handle[%d] respon rssi %d dB", connHandle, newRSSI);
}
/**************************************************
@bref       主动扫描
@param
@return
@note
**************************************************/

void bleCentralStartDiscover(void)
{
    bStatus_t status;
    status = GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE);
    LogPrintf(DEBUG_BLE, "Start discovery,ret=0x%02X", status);
    /* 清空 */
    
}
/**************************************************
@bref       主动链接从机
@param
    addr        从机地址
    addrType    从机类型
@return
@note
**************************************************/

void bleCentralStartConnect(uint8_t *addr, uint8_t addrType)
{
    char debug[13];
    bStatus_t status;
    byteToHexString(addr, debug, 6);
    debug[12] = 0;
    status = GAPRole_CentralEstablishLink(FALSE, FALSE, addrType, addr);
    LogPrintf(DEBUG_BLE, "Start connect [%s](%d),ret=0x%02X", debug, addrType, status);
    if (status != SUCCESS)
    {
        LogMessage(DEBUG_BLE, "Terminate link");
        GAPRole_TerminateLink(INVALID_CONNHANDLE);
    }
    else if (status == bleAlreadyInRequestedMode)
    {
		
    }
}
/**************************************************
@bref       主动断开与从机的链接
@param
    connHandle  从机句柄
@return
@note
**************************************************/

void bleCentralDisconnect(uint16_t connHandle)
{
    bStatus_t status;
    status = GAPRole_TerminateLink(connHandle);
    LogPrintf(DEBUG_BLE, "ble terminate Handle[%X],ret=0x%02X", connHandle, status);
}

/**************************************************
@bref       主动向从机发送数据
@param
    connHandle  从机句柄
    attrHandle  从机属性句柄
    data        数据
    len         长度
@return
    bStatus_t
@note
**************************************************/

uint8 bleCentralSend(uint16_t connHandle, uint16 attrHandle, uint8 *data, uint8 len)
{
    attWriteReq_t req;
    bStatus_t ret;
    req.handle = attrHandle;
    req.cmd = FALSE;
    req.sig = FALSE;
    req.len = len;
    req.pValue = GATT_bm_alloc(connHandle, ATT_WRITE_REQ, req.len, NULL, 0);
    if (req.pValue != NULL)
    {
        tmos_memcpy(req.pValue, data, len);
        ret = GATT_WriteCharValue(connHandle, &req, bleCentralTaskId);
        if (ret != SUCCESS)
        {
            GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
        }
    }
    LogPrintf(DEBUG_BLE, "bleCentralSend==>Ret:0x%02X", ret);
    return ret;
}


/*--------------------------------------------------*/


/**************************************************
@bref       链接列表初始化
@param
@return
@note
**************************************************/

static void bleDevConnInit(void)
{
    tmos_memset(&devInfoList, 0, sizeof(devInfoList));
}

/**************************************************
@bref       查询列表中有没有相同MAC
@param
@return
@note
**************************************************/
int8_t bleDevSearchSameBle(uint8_t *addr)
{
	int8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (strncmp(devInfoList[i].addr, addr, 6) == 0)
		{
			return 1;
		}
	}
	return 0;
}

/**************************************************
@bref       添加新的链接对象到链接列表中
@param
@return
    >0      添加成功，返回所添加的位置
    <0      添加失败
@note
**************************************************/

int8_t bleDevConnAdd(uint8_t *addr, uint8_t addrType)
{
    uint8_t i;

    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use == 0 && bleDevSearchSameBle(addr) == 0)
        {
            tmos_memcpy(devInfoList[i].addr, addr, 6);
            devInfoList[i].addrType = addrType;
            devInfoList[i].connHandle = INVALID_CONNHANDLE;
            devInfoList[i].charHandle = INVALID_CONNHANDLE;
            devInfoList[i].notifyHandle = INVALID_CONNHANDLE;
            devInfoList[i].findServiceDone = 0;
            devInfoList[i].findCharDone = 0;
            devInfoList[i].notifyDone = 0;
            devInfoList[i].connStatus = 0;
            devInfoList[i].startHandle = 0;
            devInfoList[i].endHandle = 0;
            devInfoList[i].use = 1;
            //不用添加devInfoList[i].discState = BLE_DISC_STATE_IDLE,由蓝牙状态回调来改写
            return i;
        }
    }
    return -1;
}

/**************************************************
@bref       删除链接列表中的对象
@param
@return
    >0      删除成功，返回所添加的位置
    <0      删除失败
@note
**************************************************/

int8_t bleDevConnDel(uint8_t *addr)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && tmos_memcmp(addr, devInfoList[i].addr, 6) == TRUE)
        {
            devInfoList[i].use = 0;
            if (devInfoList[i].connHandle != INVALID_CONNHANDLE)
            {
                bleCentralDisconnect(devInfoList[i].connHandle);
            }
            return i;
        }
    }
    return -1;
}

void bleDevConnDelAll(void)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use)
        {
            if (devInfoList[i].connHandle != INVALID_CONNHANDLE)
            {
                bleCentralDisconnect(devInfoList[i].connHandle);
            }
            devInfoList[i].use = 0;
        }
    }
}
/**************************************************
@bref       链接成功，查找对象并赋值句柄
@param
    addr        对象的mac地址
    connHandle  赋值对象的句柄
@return
@note
**************************************************/

static void bleDevConnSuccess(uint8_t *addr, uint16_t connHandle)
{
    uint8_t i;

    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connStatus == 0)
        {
            if (tmos_memcmp(devInfoList[i].addr, addr, 6) == TRUE)
            {
                devInfoList[i].connHandle = connHandle;
                devInfoList[i].connStatus = 1;
                devInfoList[i].notifyHandle = INVALID_CONNHANDLE;
                devInfoList[i].charHandle = INVALID_CONNHANDLE;
                devInfoList[i].discState = BLE_DISC_STATE_CONN;
                devInfoList[i].findServiceDone = 0;
                devInfoList[i].findCharDone = 0;
                devInfoList[i].notifyDone = 0;
                devInfoList[i].periodTick = 0;
                LogPrintf(DEBUG_BLE, "Get device conn handle [%d]", connHandle);
                tmos_start_task(bleCentralTaskId, BLE_TASK_SVC_DISCOVERY_EVENT, MS1_TO_SYSTEM_TIME(100));
                return;
            }
        }
        /* 如果use=0还进入这里，说明在连上之前就执行了断开的指令，所以再次断开即可 */
        if (devInfoList[i].use == 0)
        {
			
        }
    }
}

/**************************************************
@bref       链接被断开时调用
@param
    connHandle  对象的句柄
@return
@note
**************************************************/

static void bleDevDisconnect(uint16_t connHandle)
{
    uint8_t i;
    char debug[20];
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].connHandle == connHandle)
        {
            devInfoList[i].connHandle = INVALID_CONNHANDLE;
            devInfoList[i].connStatus = 0;
            devInfoList[i].notifyHandle = INVALID_CONNHANDLE;
            devInfoList[i].charHandle = INVALID_CONNHANDLE;
            devInfoList[i].findServiceDone = 0;
            devInfoList[i].findCharDone = 0;
            devInfoList[i].notifyDone = 0;
            devInfoList[i].discState = BLE_DISC_STATE_IDLE;
            tmos_memset(devInfoList[i].sockData, 0, sizeof(devSocketData_s));//断开时，要清除上报的信息
            byteToHexString(devInfoList[i].addr, debug, 6);
            debug[12] = 0;
            LogPrintf(DEBUG_BLE, "Device [%s] disconnect,Handle[%d]", debug, connHandle);
            bleSchduleChangeFsm(BLE_SCHEDULE_IDLE);
            return;
        }
    }
}

/**************************************************
@bref       获取到uuid的句柄
@param
    connHandle  对象的句柄
    handle      uuid属性句柄
@return
@note
**************************************************/

static void bleDevSetCharHandle(uint16_t connHandle, uint16_t handle)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle 
       	 						&& devInfoList[i].discState == BLE_DISC_STATE_CHAR)
        {
            devInfoList[i].charHandle = handle;
            devInfoList[i].findCharDone = 1;
            LogPrintf(DEBUG_BLE, "Dev(%d)[%d]set charHandle [0x%04X]", i, connHandle, handle);
            return;
        }
    }
}

/**************************************************
@bref       获取到notify的句柄
@param
    connHandle  对象的句柄
    handle      notify属性句柄
@return
@note
**************************************************/

static void bleDevSetNotifyHandle(uint16_t connHandle, uint16_t handle)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle
        						&& devInfoList[i].discState == BLE_DISC_STATE_CCCD
        						&& devInfoList[i].notifyHandle == INVALID_CONNHANDLE)
        {
            devInfoList[i].notifyHandle = handle;
            LogPrintf(DEBUG_BLE, "Dev(%d)[%d]set Notify Handle:[0x%04X]", i, connHandle, handle);
            return;
        }
    }
}

/**************************************************
@bref       获取到services的句柄区间
@param
    connHandle  对象的句柄
    findS       起始句柄
    findE       结束句柄
@return
@note
**************************************************/

static void bleDevSetServiceHandle(uint16_t connHandle, uint16_t findS, uint16_t findE)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle 
        						&& devInfoList[i].discState == BLE_DISC_STATE_SVC)
        {
            devInfoList[i].startHandle = findS;
            devInfoList[i].endHandle = findE;
            devInfoList[i].findServiceDone = 1;
            LogPrintf(DEBUG_BLE, "Dev(%d)[%d]Set service handle [0x%04X~0x%04X]", i, connHandle, findS, findE);
            return;
        }
    }
}

/**************************************************
@bref       查找所有服务
@param
@return
@note
**************************************************/

static void bleDevDiscoverAllServices(void)
{
    uint8_t i;
    bStatus_t status;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle != INVALID_CONNHANDLE && devInfoList[i].findServiceDone == 0)
        {
            status = GATT_DiscAllPrimaryServices(devInfoList[i].connHandle, bleCentralTaskId);
            LogPrintf(DEBUG_BLE, "Discover Handle[%d] all services,ret=0x%02X", devInfoList[i].connHandle, status);
            return;
        }
    }
}
/**************************************************
@bref       根据UUID查找服务
@param
@return
@note
**************************************************/
static void bleDevDiscoverServByUuid(void)
{
	uint8_t i;
    uint8_t uuid[2];
    bStatus_t status;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle != INVALID_CONNHANDLE
        						&& devInfoList[i].findServiceDone == 0
        						&& devInfoList[i].discState == BLE_DISC_STATE_CONN)
        {
        	devInfoList[i].discState = BLE_DISC_STATE_SVC;
            uuid[0] = LO_UINT16(SERVICE_UUID);
            uuid[1] = HI_UINT16(SERVICE_UUID);
            status = GATT_DiscPrimaryServiceByUUID(devInfoList[i].connHandle, uuid, 2, bleCentralTaskId);
            LogPrintf(DEBUG_BLE, "Dev(%d)Discover Handle[%d]services by uuid,ret=0x%02X", i, devInfoList[i].connHandle, status);
            return;
        }
    }
    LogPrintf(DEBUG_BLE, "bleDevDiscoverServByUuid==>Error");
}

/**************************************************
@bref       根据UUID找特征
@param
@return
@note
**************************************************/
static void bleDevDiscoverCharByUuid(void)
{
	uint8_t i;
	attReadByTypeReq_t req;
	bStatus_t status;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use && devInfoList[i].startHandle != 0
								&& devInfoList[i].findServiceDone == 1
								&& devInfoList[i].discState == BLE_DISC_STATE_SVC)
		{
			devInfoList[i].discState = BLE_DISC_STATE_CHAR;
			req.startHandle = devInfoList[i].startHandle;
            req.endHandle = devInfoList[i].endHandle;
            req.type.len = ATT_BT_UUID_SIZE;
            req.type.uuid[0] = LO_UINT16(CHAR_UUID);
            req.type.uuid[1] = HI_UINT16(CHAR_UUID);
            status = GATT_ReadUsingCharUUID(devInfoList[i].connHandle, &req, bleCentralTaskId);
            LogPrintf(DEBUG_BLE, "Dev(%d)Discover handle[%d]chars by uuid,ret=0x%02X", i, devInfoList[i].connHandle, status);
			return;
		}
	}
	LogPrintf(DEBUG_BLE, "bleDevDiscoverCharByUuid==>Error");
}


/**************************************************
@bref       查找所有特征
@param
@return
@note
**************************************************/

static void bleDevDiscoverAllChars(uint16_t connHandle)
{
    uint8_t i;
    bStatus_t status;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle && devInfoList[i].findServiceDone == 1)
        {
            status = GATT_DiscAllChars(devInfoList[i].connHandle, devInfoList[i].startHandle, devInfoList[i].endHandle,
                                       bleCentralTaskId);
            LogPrintf(DEBUG_BLE, "Discover handle[%d] all chars,ret=0x%02X", devInfoList[i].connHandle, status);
            return;
        }
    }
}

/**************************************************
@bref       查找所有notify
@param
@return
@note
**************************************************/

static void bleDevDiscoverNotify(uint16_t connHandle)
{
    uint8_t i;
    bStatus_t status;
    attReadByTypeReq_t req;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle
                				&& devInfoList[i].findCharDone == 1)
        {
            tmos_memset(&req, 0, sizeof(attReadByTypeReq_t));
			req.startHandle = devInfoList[i].startHandle;
			req.endHandle = devInfoList[i].endHandle;
			req.type.len = ATT_BT_UUID_SIZE;
			req.type.uuid[0] = LO_UINT16(GATT_CLIENT_CHAR_CFG_UUID);
			req.type.uuid[1] = HI_UINT16(GATT_CLIENT_CHAR_CFG_UUID);
			devInfoList[i].discState = BLE_DISC_STATE_CCCD;
			status = GATT_ReadUsingCharUUID(devInfoList[i].connHandle, &req, bleCentralTaskId);
            LogPrintf(DEBUG_BLE, "Dec(%d)[%d]Discover notify,ret=0x%02X", i, devInfoList[i].connHandle, status);
            return;
        }
    }
	LogPrintf(DEBUG_BLE, "bleDevDiscoverNotify==>Error");
}

/**************************************************
@bref       使能notify
@param
@return
@note
**************************************************/

static uint8_t bleDevEnableNotify(void)
{
    uint8_t i;
    uint8_t data[2];
    bStatus_t status = SUCCESS;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].notifyHandle != INVALID_CONNHANDLE 
        					   && devInfoList[i].notifyDone   == 0
        					   && devInfoList[i].connHandle   != INVALID_CONNHANDLE
        					   && devInfoList[i].discState    == BLE_DISC_STATE_CCCD)
        {
            data[0] = 0x01;
            data[1] = 0x00;
            LogPrintf(DEBUG_BLE, "Dev(%d)Handle[%d] try to notify", i, devInfoList[i].connHandle);
            status = bleCentralSend(devInfoList[i].connHandle, devInfoList[i].notifyHandle, data, 2);
            if (/*status == SUCCESS*/1)//默认notify成功
            {
                devInfoList[i].notifyDone = 1;
                devInfoList[i].discState  = BLE_DISC_STATE_COMP;
            }
            bleSendDataReqSet(i, BLE_SEND_LOGININFO_EVENT | BLE_SEND_HBT_EVNET);
            return status;
        }
    }
    return 0;
}


/**************************************************
@bref       使能notify
@param
@return
@note
**************************************************/

static uint8_t bleDevSendDataTest(void)
{
    uint8_t i;
    uint8_t data[] = {0x0c, 0x01, 0x00, 0x01, 0x0d};
    bStatus_t status;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].charHandle != INVALID_CONNHANDLE)
        {
            status = bleCentralSend(devInfoList[i].connHandle, devInfoList[i].charHandle, data, 5);
            return status;
        }
    }
    return 0;
}


/**************************************************
@bref       添加设备信息
@param
@return
@note
**************************************************/

void bleDevScanInfoAdd(deviceScanInfo_s *devInfo)
{
    uint8_t i, j;
    if (scanList.cnt >= SCAN_LIST_MAX_SIZE)
    {
    	LogPrintf(DEBUG_BLE, "bleDevScanInfoAdd==>fail");
        return;
    }
    LogPrintf(DEBUG_BLE, "bleDevScanInfoAdd==>OK");
    for (i = 0; i < scanList.cnt; i++)
    {
        if (devInfo->rssi > scanList.list[i].rssi)
        {
            for (j = scanList.cnt; j > i; j--)
            {
                scanList.list[j] = scanList.list[j - 1];
            }
            tmos_memcpy(&scanList.list[i], devInfo, sizeof(deviceScanInfo_s));
            scanList.cnt++;
            return;
        }
    }
    //copy to last
    tmos_memcpy(&scanList.list[scanList.cnt++], devInfo, sizeof(deviceScanInfo_s));
}


/**************************************************
@bref       终止链路
@param
@return
@note  不修改useflag及其他的标志位,且每秒中断一个
**************************************************/

void bleDevTerminateByConnPermit(void)
{
    uint8_t i;
    bStatus_t status;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle != INVALID_CONNHANDLE && devInfoList[i].connPermit == 0)
        {
            status = GAPRole_TerminateLink(devInfoList[i].connHandle);
            LogPrintf(DEBUG_BLE, "Terminate Hanle[%d]", devInfoList[i].connHandle);
            return;
        }
    }
}

/**************************************************
@bref       通过地址查找对象
@param
@return
@note
**************************************************/

deviceConnInfo_s *bleDevGetInfo(uint8_t *addr)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && tmos_memcmp(devInfoList[i].addr, addr, 6) == TRUE)
        {
            return &devInfoList[i];
        }
    }
    return NULL;
}

/**************************************************
@bref       获取调度器中蓝牙个数
@param
@return
@note
**************************************************/
uint8_t bleDevGetCnt(void)
{
	uint8_t cnt = 0, i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use) {
			cnt++;
		}
	}
	return cnt;
}

/**************************************************
@bref       根据SocketId找到蓝牙
@param
@return
@note
**************************************************/

deviceConnInfo_s *bleDevGetInfoBySockid(uint8_t sockid)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].socketId == sockid && devInfoList[i].use)
		{
			return &devInfoList[i];
		}
	}
	return NULL;
}

/**************************************************
@bref       通过链接句柄，获取对应蓝牙信息
@param
@return
@note
**************************************************/

deviceConnInfo_s *bleDevGetInfoByHandle(uint8_t handle)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].connHandle == handle && devInfoList[i].use)
		{
			return &devInfoList[i];
		}
	}
	return NULL;
}

/**************************************************
@bref       通过指针，获取对应蓝牙信息
@param
@return
@note
**************************************************/

deviceConnInfo_s *bleDevGetInfoByIndex(uint8_t index)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use)
		{
			return &devInfoList[i];
		}
	}
	return NULL;
}


/**************************************************
@bref       通过链接句柄，获取对应mac地址
@param
@return
@note
**************************************************/

uint8_t *bleDevGetAddrByHandle(uint16_t connHandle)
{
    uint8_t i;
    for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
    {
        if (devInfoList[i].use && devInfoList[i].connHandle == connHandle)
        {
            return devInfoList[i].addr;
        }
    }
    return NULL;
}

/**************************************************
@bref       通过链接句柄，获取对应蓝牙指针
@param
@return
@note
**************************************************/

int8_t bleDevGetIndexByHandle(uint8_t connHandle)
{
	int8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use && devInfoList[i].connHandle == connHandle)
        {
            return i;
        }
	}
	return -1;
}

/**************************************************
@bref       获取所有蓝牙信息
@param
@return
@note
**************************************************/

deviceConnInfo_s *bleDevGetInfoAll(void)
{
	return devInfoList;
}

/**************************************************
@bref       允许指定ID蓝牙进行连接
@param
@return
@note
**************************************************/

void bleDevSetPermit(uint8 id, uint8_t enabled)
{
	if (enabled)
	{
		if (devInfoList[id].use)
		{
			devInfoList[id].connPermit = 1;
		}
	}
	else
	{
		if (devInfoList[id].use)
		{
			devInfoList[id].connPermit = 0;
		}
	}
	//LogPrintf(DEBUG_BLE, "bleDevSetPermit==>id:%d %s", id, enabled ? "enable" : "disable");
}

/**************************************************
@bref       获取指定蓝牙是否能连接
@param
@return
@note
**************************************************/

uint8_t bleDevGetPermit(uint8_t id)
{
	if (devInfoList[id].use)
		return devInfoList[id].connPermit;
	return 0;
}
/**************************************************
@bref       通过链接句柄找到设备ID
@param
@return
@note
**************************************************/

int bleDevGetIdByHandle(uint16_t connHandle)
{
	uint8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (devInfoList[i].use && devInfoList[i].connHandle == connHandle)
		{
			return i;
		}
	}
	return  -1;
}

/**************************************************
@bref       获取绑定蓝牙个数
@param
@return
@note
**************************************************/

uint8_t bleDevGetBleMacCnt(void)
{
	uint8_t i, cnt = 0;
	for (i = 0; i < sizeof(sysparam.bleConnMac) / sizeof(sysparam.bleConnMac[0]); i++)
	{
		if (sysparam.bleConnMac[i][0] != 0)
		{
			cnt++;
		}
	}
	return cnt;
}
int8_t bleInsert(uint8_t *addr, uint8_t addrType)
{
    int8_t ret = -1;
    uint8_t i;
    for (i = 0; i < BLE_CONNECT_LIST_SIZE; i++)
    {
        if (devInfoList[i].use == 0)
        {
            
            devInfoList[i].use = 1;
            tmos_memcpy(devInfoList[i].addr, addr, 6);
            devInfoList[i].addrType = addrType;
            ret = bleDevConnAdd(addr, addrType);
//            bleRelaySetReq(i, BLE_EVENT_SET_RF_THRE | BLE_EVENT_SET_OUTV_THRE | BLE_EVENT_SET_AD_THRE | BLE_EVENT_GET_AD_THRE |
//                           BLE_EVENT_GET_RF_THRE | BLE_EVENT_GET_OUT_THRE | BLE_EVENT_GET_OUTV | BLE_EVENT_GET_RFV |
//                           BLE_EVENT_GET_PRE_PARAM | BLE_EVENT_SET_PRE_PARAM | BLE_EVENT_GET_PRE_PARAM | BLE_EVENT_CHK_SOCKET);
            return ret;
        }
    }
    return ret;
}


uint8 bleHandShakeTask(void)
{
    uint8_t i;
    static uint16_t timeouttick_dev1 = 0;
    static uint16_t timeouttick_dev2 = 0;
    bleRelayInfo_s *relayinfo;
    deviceConnInfo_s *devinfo;
    if (primaryServerIsReady())
    {
        bleDevSetPermit(0, 1);
        bleDevSetPermit(1, 1);
        timeouttick_dev1 = 0;
        timeouttick_dev2 = 0;
        return 1;
    }
//  if (primaryServerIsReady() == 0)
//  {
//      for (i = 0; i < bleDevGetCnt(); i++)
//      {
//          relayinfo = bleRelayGeInfo(i);
//          if ((sysparam.bleAutoDisc != 0 &&
//                      (sysinfo.sysTick - relayinfo->updateTick) >= sysparam.bleAutoDisc * 60 / 2) ||
//                        sysinfo.bleforceCmd != 0)
//          {
//              bleDevSetPermit(i, 1);
//              /* 每隔一段时间刷新定时器 */
//              if ((sysinfo.sysTick - relayinfo->updateTick) % (sysparam.bleAutoDisc * 60 / 2) == 0 &&
//                   sysinfo.sysTick != 0)
//              {
//                  if (i == 0)
//                  {
//                      timeouttick_dev1 = 0;
//                  }
//                  else
//                  {
//                      timeouttick_dev2 = 0;
//                  }
//              }
//          }
//          else
//          {
//              bleDevSetPermit(i, 0);
//          }
//      }
//      if (bleDevGetPermit(0))
//      {
//          if (timeouttick_dev1++ >= 180)
//          {
//              bleDevSetPermit(0, 0);
//          }
//      }
//      else {
//          timeouttick_dev1 = 0;
//      }
//      if (bleDevGetPermit(1))
//      {
//          if (timeouttick_dev2++ >= 180)
//          {
//              bleDevSetPermit(1, 0);
//          }
//      }
//      else {
//          timeouttick_dev2 = 0;
//      }
//  }
//  LogPrintf(DEBUG_ALL, "tick:%d %d permit:%d %d", timeouttick_dev1, timeouttick_dev2, bleDevGetPermit(0), bleDevGetPermit(1));
}

/**************************************************
@bref       蓝牙链接管理调度器状态切换
@param
@return
@note
**************************************************/

static void bleSchduleChangeFsm(bleFsm nfsm)
{
    bleSchedule.fsm = nfsm;
    bleSchedule.runTick = 0;
    LogPrintf(DEBUG_BLE, "bleSchduleChangeFsm==>%d", nfsm);
}

/**************************************************
@bref       蓝牙链接管理调度器
@param
@return
@note
**************************************************/

static void bleScheduleTask(void)
{
    static uint8_t ind = 0;
    
	bleDevScanProcess();
//	bleHandShakeTask();
//	bleDevTerminateByConnPermit();
    switch (bleSchedule.fsm)
    {
    	case BLE_SCHEDULE_IDLE:
			ind = ind % DEVICE_MAX_CONNECT_COUNT;
			//查找是否有未链接的设备，需要进行链接
			for (; ind < DEVICE_MAX_CONNECT_COUNT; ind++)
			{
				if (devInfoList[ind].use && 
					devInfoList[ind].connHandle == INVALID_CONNHANDLE && 
					devInfoList[ind].discState  == BLE_DISC_STATE_IDLE /*&&
					devInfoList[ind].connPermit == 1*/)
				{
					bleCentralStartConnect(devInfoList[ind].addr, devInfoList[ind].addrType);
					bleSchduleChangeFsm(BLE_SCHEDULE_WAIT);
					break;
				}
			}
			break;
        case BLE_SCHEDULE_WAIT:
            if (bleSchedule.runTick >= 15)
            {
                //链接超时
                LogPrintf(DEBUG_BLE, "bleSchedule==>timeout!!!");
                bleCentralDisconnect(devInfoList[ind].connHandle);
                bleSchduleChangeFsm(BLE_SCHEDULE_DONE);
                if (sysinfo.logLevel == 4)
                {
                    LogMessage(DEBUG_FACTORY, "+FMPC:BLE CONNECT FAIL");
                }
            }
            else
            {
                break;
            }
        case BLE_SCHEDULE_DONE:
            ind++;
            bleSchduleChangeFsm(BLE_SCHEDULE_IDLE);
            break;
        default:
            bleSchduleChangeFsm(BLE_SCHEDULE_IDLE);
            break;
    }
    bleSchedule.runTick++;
}


/**************************************************
@bref       BLE连接状态机切换
@param
@return
@note
**************************************************/
static uint8_t bleScanFsm = BLE_SCAN_IDLE;
static uint8_t bleScanTick = 0;

static void bleScanFsmChange(uint8_t fsm)
{
	bleScanFsm  = fsm;
	bleScanTick = 0;
	LogPrintf(DEBUG_BLE, "bleScanFsmChange==>%d", fsm);
}

/**************************************************
@bref       蓝牙扫描采集连接信息
@param
@return
@note
**************************************************/

uint8_t bleDevScanProcess(void)
{
	uint8_t result = 0;
	switch (bleScanFsm)
	{
		case BLE_SCAN_IDLE:
			if (bleDevGetBleMacCnt() == 0 && bleDevGetCnt() < DEVICE_MAX_CONNECT_COUNT)
			{
				bleCentralStartDiscover();
				bleScanFsmChange(BLE_SCAN_ING);
			}
			break;
		case BLE_SCAN_ING:
			if (bleScanTick++ >= 30)
			{
				bleScanFsmChange(BLE_SCAN_IDLE);
			}
			break;
		case BLE_SCAN_DONE:
			/* 加入蓝牙调度器 */
			bleScanFsmChange(BLE_SCAN_IDLE);
			break;

		case BLE_SCAN_WAIT:

			break;
		default:

			break;
	}
}



