/*
 * app_server.c
 *
 *  Created on: Jul 14, 2022
 *      Author: idea
 */
#include "app_server.h"
#include "app_kernal.h"
#include "app_net.h"
#include "app_protocol.h"
#include "app_param.h"
#include "app_sys.h"
#include "app_socket.h"
#include "app_gps.h"
#include "app_db.h"
#include "app_jt808.h"
#include "app_task.h"
#include "app_central.h"

static netConnectInfo_s privateServConn, hiddenServConn;
static bleSocketInfo_s blePetServConn[DEVICE_MAX_CONNECT_COUNT];
static jt808_Connect_s jt808ServConn;
static int8_t timeOutId = -1;
static int8_t hbtTimeOutId = -1;

/**************************************************
@bref		模组回复，停止定时器，防止模组死机用
@param
@return
@note
**************************************************/

void moduleRspSuccess(void)
{
    if (timeOutId != -1)
    {
        stopTimer(timeOutId);
        timeOutId = -1;
    }
}
/**************************************************
@bref		心跳回复，停止定时器，防止模组死机用
@param
@return
@note
**************************************************/

void hbtRspSuccess(void)
{
    if (hbtTimeOutId != -1)
    {
        stopTimer(hbtTimeOutId);
        hbtTimeOutId = -1;
    }
}


/**************************************************
@bref		执行复位模组
@param
@return
@note
**************************************************/

static void moduleRspTimeout(void)
{
    timeOutId = -1;
    LogMessage(DEBUG_ALL, "Modlue rsp timeout");
    moduleReset();
}

static void hbtRspTimeOut(void)
{
    LogMessage(DEBUG_ALL, "heartbeat timeout");
    hbtTimeOutId = -1;
    if (sysparam.protocol == ZT_PROTOCOL_TYPE)
    {
        socketDel(NORMAL_LINK);
    }
    else
    {
        socketDel(JT808_LINK);
    }
    moduleSleepCtl(0);
}


/**************************************************
@bref		联网状态机切换
@param
@return
@note
**************************************************/

static void privateServerChangeFsm(NetWorkFsmState state)
{
    privateServConn.fsmstate = state;
}


/**************************************************
@bref		停止定时器，防止模组死机用
@param
@return
@note
**************************************************/

void privateServerReconnect(void)
{
    LogMessage(DEBUG_ALL, "private reconnect server");
    socketDel(NORMAL_LINK);
    moduleSleepCtl(0);
}






/**************************************************
@bref		服务器断开
@param
@return
@note
**************************************************/

void privateServerDisconnect(void)
{
    privateServerChangeFsm(SERV_LOGIN);
}
/**************************************************
@bref		主服务器登录正常
@param
@return
@note
**************************************************/

void privateServerLoginSuccess(void)
{
    privateServConn.loginCount = 0;
    privateServConn.heartbeattick = 0;
    moduleSleepCtl(1);
    ledStatusUpdate(SYSTEM_LED_NETOK, 1);
    privateServerChangeFsm(SERV_READY);
}
/**************************************************
@bref		socket数据接收
@param
@return
@note
**************************************************/

static void privateServerSocketRecv(char *data, uint16_t len)
{
    uint16_t i, beginindex, contentlen, lastindex;
    //遍历，寻找协议头
    for (i = 0; i < len; i++)
    {
        beginindex = i;
        if (data[i] == 0x78)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x78)
            {
                continue ;
            }
            if (i + 2 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2];
            if ((i + 5 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 3 + contentlen] == 0x0D && data[i + 4 + contentlen] == 0x0A)
            {
                i += (4 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7878[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(NORMAL_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
        else if (data[i] == 0x79)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x79)
            {
                continue ;
            }
            if (i + 3 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2] << 8 | data[i + 3];
            if ((i + 6 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 4 + contentlen] == 0x0D && data[i + 5 + contentlen] == 0x0A)
            {
                i += (5 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7979[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(NORMAL_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
    }
}
/**************************************************
@bref		主服务器连接任务
@param
@return
@note
**************************************************/

void privateServerConnTask(void)
{
    static uint16_t unLoginTick = 0;
    if (isModuleRunNormal() == 0)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        return ;
    }
    if (socketGetUsedFlag(NORMAL_LINK) != 1)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        privateServerChangeFsm(SERV_LOGIN);
        socketAdd(NORMAL_LINK, sysparam.Server, sysparam.ServerPort, privateServerSocketRecv);
        return;
    }
    if (socketGetConnStatus(NORMAL_LINK) != SOCKET_CONN_SUCCESS)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        privateServerChangeFsm(SERV_LOGIN);
        if (unLoginTick++ >= 900)
        {
            unLoginTick = 0;
            moduleReset();
        }
        return;
    }
    switch (privateServConn.fsmstate)
    {
        case SERV_LOGIN:
            unLoginTick = 0;
            if (strncmp(dynamicParam.SN, "888888887777777", 15) == 0)
            {
                LogMessage(DEBUG_ALL, "no Sn");
                return;
            }

            LogMessage(DEBUG_ALL, "Login to server...");
            protocolSnRegister(dynamicParam.SN);
            protocolSend(NORMAL_LINK, PROTOCOL_01, NULL);
            protocolSend(NORMAL_LINK, PROTOCOL_F1, NULL);
            protocolSend(NORMAL_LINK, PROTOCOL_8A, NULL);
            privateServerChangeFsm(SERV_LOGIN_WAIT);
            privateServConn.logintick = 0;
            break;
        case SERV_LOGIN_WAIT:
            privateServConn.logintick++;
            if (privateServConn.logintick >= 60)
            {
                privateServerChangeFsm(SERV_LOGIN);
                privateServConn.loginCount++;
                privateServerReconnect();
                if (privateServConn.loginCount >= 3)
                {
                    privateServConn.loginCount = 0;
                    moduleReset();
                }
            }
            break;
        case SERV_READY:
            if (privateServConn.heartbeattick % (sysparam.heartbeatgap - 2) == 0)
            {
                queryBatVoltage();
                moduleGetCsq();
            }
            if (privateServConn.heartbeattick % sysparam.heartbeatgap == 0)
            {
                privateServConn.heartbeattick = 0;
                if (timeOutId == -1)
                {
                    timeOutId = startTimer(120, moduleRspTimeout, 0);
                }
                if (hbtTimeOutId == -1)
                {
                    hbtTimeOutId = startTimer(1800, hbtRspTimeOut, 0);
                }
                protocolInfoResiter(getBatteryLevel(), sysinfo.outsidevoltage > 5.0 ? sysinfo.outsidevoltage : sysinfo.insidevoltage,
                                    dynamicParam.startUpCnt, dynamicParam.runTime);
                protocolSend(NORMAL_LINK, PROTOCOL_13, NULL);
            }
            privateServConn.heartbeattick++;
            if (getTcpNack())
            {
                querySendData(NORMAL_LINK);
            }
            if (privateServConn.heartbeattick % 2 == 0)
            {
                //传完ram再传文件系统
                if (getTcpNack() == 0)
                {
                    if (dbUpload() == 0)
                    {
                        gpsRestoreUpload();
                    }
                }
            }
            break;
        default:
            privateServConn.fsmstate = SERV_LOGIN;
            privateServConn.heartbeattick = 0;
            break;
    }
}


static uint8_t hiddenServCloseChecking(void)
{
    if (sysparam.hiddenServOnoff == 0)
    {
        return 1;
    }
    if (sysparam.protocol == ZT_PROTOCOL_TYPE)
    {
        if (sysparam.ServerPort == sysparam.hiddenPort)
        {
            if (strcmp(sysparam.Server, sysparam.hiddenServer) == 0)
            {
                //if use the same server and port ,abort use hidden server.
                return	1;
            }
        }
    }
    if (sysinfo.hiddenServCloseReq)
    {
        //it is the system request of close hidden server,maybe the socket error.
        return 1;
    }
    return 0;
}

static void hiddenServerChangeFsm(NetWorkFsmState state)
{
    hiddenServConn.fsmstate = state;
}

/**************************************************
@bref		socket数据接收
@param
@return
@note
**************************************************/

static void hiddenServerSocketRecv(char *data, uint16_t len)
{
    uint16_t i, beginindex, contentlen, lastindex;
    //遍历，寻找协议头
    for (i = 0; i < len; i++)
    {
        beginindex = i;
        if (data[i] == 0x78)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x78)
            {
                continue ;
            }
            if (i + 2 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2];
            if ((i + 5 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 3 + contentlen] == 0x0D && data[i + 4 + contentlen] == 0x0A)
            {
                i += (4 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7878[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(HIDDEN_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
        else if (data[i] == 0x79)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x79)
            {
                continue ;
            }
            if (i + 3 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2] << 8 | data[i + 3];
            if ((i + 6 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 4 + contentlen] == 0x0D && data[i + 5 + contentlen] == 0x0A)
            {
                i += (5 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7979[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(HIDDEN_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
    }

}

/**************************************************
@bref		隐藏服务器登录回复
@param
	none
@return
	none
@note
**************************************************/

void hiddenServerLoginSuccess(void)
{
    hiddenServConn.loginCount = 0;
    hiddenServConn.heartbeattick = 0;
    hiddenServerChangeFsm(SERV_READY);
}

/**************************************************
@bref		请求关闭隐藏链路
@param
@return
@note
**************************************************/

void hiddenServerCloseRequest(void)
{
    sysinfo.hiddenServCloseReq = 1;
    LogMessage(DEBUG_ALL, "hidden serv close request");
}


/**************************************************
@bref		清除关闭隐藏链路的请求
@param
@return
@note
**************************************************/

void hiddenServerCloseClear(void)
{
    sysinfo.hiddenServCloseReq = 0;
    LogMessage(DEBUG_ALL, "hidden serv close clear");
}

/**************************************************
@bref		隐藏服务器连接任务
@param
@return
@note
**************************************************/

static void hiddenServerConnTask(void)
{
    if (isModuleRunNormal() == 0)
    {
        return ;
    }

    if (hiddenServCloseChecking())
    {
        if (socketGetUsedFlag(HIDDEN_LINK) == 1)
        {
            LogMessage(DEBUG_ALL, "hidden server abort");
            socketDel(HIDDEN_LINK);
        }
        hiddenServerChangeFsm(SERV_LOGIN);
        return;
    }
    if (socketGetUsedFlag(HIDDEN_LINK) != 1)
    {
        hiddenServerChangeFsm(SERV_LOGIN);
        socketAdd(HIDDEN_LINK, sysparam.hiddenServer, sysparam.hiddenPort, hiddenServerSocketRecv);
        return;
    }
    if (socketGetConnStatus(HIDDEN_LINK) != SOCKET_CONN_SUCCESS)
    {
        hiddenServerChangeFsm(SERV_LOGIN);
        return;
    }
    switch (hiddenServConn.fsmstate)
    {
        case SERV_LOGIN:
            LogMessage(DEBUG_ALL, "Login to server...");
            protocolSnRegister(dynamicParam.SN);
            protocolSend(HIDDEN_LINK, PROTOCOL_01, NULL);
            hiddenServerChangeFsm(SERV_LOGIN_WAIT);
            hiddenServConn.logintick = 0;
            break;
        case SERV_LOGIN_WAIT:
            hiddenServConn.logintick++;
            if (hiddenServConn.logintick >= 60)
            {
                hiddenServerChangeFsm(SERV_LOGIN);
                hiddenServConn.loginCount++;
                privateServerReconnect();
                if (hiddenServConn.loginCount >= 3)
                {
                    hiddenServConn.loginCount = 0;
                    hiddenServerCloseRequest();
                }
            }
            break;
        case SERV_READY:
            if (hiddenServConn.heartbeattick % (sysparam.heartbeatgap - 2) == 0)
            {
                queryBatVoltage();
                moduleGetCsq();
            }
            if (hiddenServConn.heartbeattick % sysparam.heartbeatgap == 0)
            {
                hiddenServConn.heartbeattick = 0;
                if (timeOutId == -1)
                {
                    timeOutId = startTimer(80, moduleRspTimeout, 0);
                }
                protocolInfoResiter(getBatteryLevel(), sysinfo.outsidevoltage > 5.0 ? sysinfo.outsidevoltage : sysinfo.insidevoltage,
                                    dynamicParam.startUpCnt, dynamicParam.runTime);
                protocolSend(HIDDEN_LINK, PROTOCOL_13, NULL);
            }
            hiddenServConn.heartbeattick++;

            break;
        default:
            hiddenServConn.fsmstate = SERV_LOGIN;
            hiddenServConn.heartbeattick = 0;
            break;
    }
}



/**************************************************
@bref		jt808状态机切换状态
@param
	nfsm	新状态
@return
	none
@note
**************************************************/

static void jt808ServerChangeFsm(jt808_connfsm_s nfsm)
{
    jt808ServConn.connectFsm = nfsm;
    jt808ServConn.runTick = 0;
}

/**************************************************
@bref		jt808数据接收回调
@param
	none
@return
	none
@note
**************************************************/

static void jt808ServerSocketRecv(char *rxbuf, uint16_t len)
{
    jt808ReceiveParser(JT808_LINK, (uint8_t *)rxbuf, len);
}

/**************************************************
@bref		数据发送接口
@param
	none
@return
	1		发送成功
	!=1		发送失败
@note
**************************************************/

static int jt808ServerSocketSend(uint8_t link, uint8_t *data, uint16_t len)
{
    int ret;
    ret = socketSendData(link, data, len);
    return 1;
}

/**************************************************
@bref		jt808联网状态机
@param
@return
@note
**************************************************/

void jt808ServerReconnect(void)
{
    LogMessage(DEBUG_ALL, "jt808 reconnect server");
    socketDel(JT808_LINK);
    moduleSleepCtl(0);
}

/**************************************************
@bref		jt808鉴权成功回复
@param
@return
@note
**************************************************/

void jt808ServerAuthSuccess(void)
{
    jt808ServConn.authCnt = 0;
    moduleSleepCtl(1);
    jt808ServerChangeFsm(JT808_NORMAL);
    ledStatusUpdate(SYSTEM_LED_NETOK, 1);
}

/**************************************************
@bref		jt808服务器连接任务
@param
@return
@note
**************************************************/

void jt808ServerConnTask(void)
{
    static uint8_t ret = 1;
    static uint16_t unLoginTick = 0;
    if (isModuleRunNormal() == 0)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        return;
    }
    if (socketGetUsedFlag(JT808_LINK) != 1)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        jt808ServerChangeFsm(JT808_REGISTER);
        jt808RegisterTcpSend(JT808_LINK, jt808ServerSocketSend);
        jt808RegisterManufactureId(JT808_LINK, (uint8_t *)"ZT");
        jt808RegisterTerminalType(JT808_LINK, (uint8_t *)"06");
        jt808RegisterTerminalId(JT808_LINK, (uint8_t *)"01");
        socketAdd(JT808_LINK, sysparam.jt808Server, sysparam.jt808Port, jt808ServerSocketRecv);
        return;
    }
    if (socketGetConnStatus(JT808_LINK) != SOCKET_CONN_SUCCESS)
    {
        ledStatusUpdate(SYSTEM_LED_NETOK, 0);
        jt808ServerChangeFsm(JT808_REGISTER);
        if (unLoginTick++ >= 900)
        {
            unLoginTick = 0;
            moduleReset();
        }
        return;
    }


    switch (jt808ServConn.connectFsm)
    {
        case JT808_REGISTER:

            if (strcmp((char *)dynamicParam.jt808sn, "888777") == 0)
            {
                LogMessage(DEBUG_ALL, "no JT808SN");
                return;
            }

            if (dynamicParam.jt808isRegister)
            {
                //已注册过的设备不用重复注册
                jt808ServerChangeFsm(JT808_AUTHENTICATION);
                jt808ServConn.regCnt = 0;
            }
            else
            {
                //注册设备
                if (jt808ServConn.runTick % 60 == 0)
                {
                    if (jt808ServConn.regCnt++ > 3)
                    {
                        LogMessage(DEBUG_ALL, "Terminal register timeout");
                        jt808ServConn.regCnt = 0;
                        jt808ServerReconnect();
                    }
                    else
                    {
                        LogMessage(DEBUG_ALL, "Terminal register");
                        jt808RegisterLoginInfo(JT808_LINK, dynamicParam.jt808sn, dynamicParam.jt808isRegister, dynamicParam.jt808AuthCode, dynamicParam.jt808AuthLen);
                        jt808SendToServer(JT808_LINK, TERMINAL_REGISTER, NULL);
                    }
                }
                break;
            }
        case JT808_AUTHENTICATION:

            if (jt808ServConn.runTick % 60 == 0)
            {
                ret = 1;
                if (jt808ServConn.authCnt++ > 3)
                {
                    jt808ServConn.authCnt = 0;
                    dynamicParam.jt808isRegister = 0;
                    dynamicParamSaveAll();
                    jt808ServerReconnect();
                    LogMessage(DEBUG_ALL, "Authentication timeout");
                }
                else
                {
                    LogMessage(DEBUG_ALL, "Terminal authentication");
                    jt808ServConn.hbtTick = sysparam.heartbeatgap;
                    jt808RegisterLoginInfo(JT808_LINK, dynamicParam.jt808sn, dynamicParam.jt808isRegister, dynamicParam.jt808AuthCode, dynamicParam.jt808AuthLen);
                    jt808SendToServer(JT808_LINK, TERMINAL_AUTH, NULL);
                }
            }
            break;
        case JT808_NORMAL:
            if (++jt808ServConn.hbtTick >= sysparam.heartbeatgap)
            {
                jt808ServConn.hbtTick = 0;
                queryBatVoltage();
                LogMessage(DEBUG_ALL, "Terminal heartbeat");
                jt808SendToServer(JT808_LINK, TERMINAL_HEARTBEAT, NULL);
                if (timeOutId == -1)
                {
                    timeOutId = startTimer(80, moduleRspTimeout, 0);
                }

                if (hbtTimeOutId == -1)
                {
                    hbtTimeOutId = startTimer(1800, hbtRspTimeOut, 0);
                }
            }
            if (getTcpNack())
            {
                querySendData(JT808_LINK);
            }
            if (jt808ServConn.runTick % 3 == 0)
            {
                //传完ram再传文件系统
                if (getTcpNack() == 0)
                {
                    if (dbUpload() == 0)
                    {
                        gpsRestoreUpload();
                    }
                }
            }
            break;
        default:
            jt808ServerChangeFsm(JT808_AUTHENTICATION);
            break;
    }
    jt808ServConn.runTick++;
}


/**
 * 蓝牙-4G总体连接逻辑
 * 蓝牙先扫描，把扫到的PT13（最多2个）加入到连接列表，连接列表满了，停止扫描工作（要考虑链接的过程中一直在扫描会不会影响蓝牙链接的稳定性）
 * 蓝牙链接列表有目标设备之后，就去连接，如果连续3次连接超时，清出蓝牙连接列表，重新扫描
 * 蓝牙连接成功便获取数据，中间断连会一直重连，直到3分钟后仍无法获取数据，就将该设备移出连接列表并重新扫描
 * 蓝牙获取数据成功后加入socket链路管理。中间sock链路断开一直重连就好，除非蓝牙连接列表把该设备删除
 * 蓝牙连接列表删除，对应的socket也要删除
 */


/**************************************************
@bref		根据数组指针获取链路ID
@param
@return
@note
	链路与数组编号绑定，数组0为链路1，数组1为链路5
**************************************************/

uint8_t BleSockId(uint8_t i)	
{
	if (i < DEVICE_MAX_CONNECT_COUNT)
	{
		if (i == 0) return BLE1_LINK;
		else if (i == 1) return BLE2_LINK;
		else return BLE1_LINK;
	}
	else return BLE1_LINK;
}


/**************************************************
@bref		查找空闲的蓝牙链路
@param
@return
@note
**************************************************/

int8_t blePetSearchIdleServer(void)
{
	int8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (blePetServConn[i].use == 0)
		{
			return BleSockId(i);
		}
	}
	return -1;
}


/**************************************************
@bref		查找正在使用该sn登录的链路
@param
@return
@note
!!!		返回的是链路id
**************************************************/

int8_t blePetSearchServerSn(char *Sn)
{
	int8_t i;
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (strncmp(blePetServConn[i].loginSn, Sn, 15) == 0 && blePetServConn[i].use)
		{
			return i;
		}
	}
	return -1;
}



/**************************************************
@bref		添加蓝牙链路
@param
@return
@note

添加：3分钟后还是这个SN号，链路断开切换这

**************************************************/
int8_t blePetServerAdd(char *Sn)
{
	/* 如果链路中已存在该SN号,无需修改 */
	if (blePetSearchServerSn(Sn) >= 0)
	{
		LogPrintf(DEBUG_BLE, "blePetServerAdd==>Already exist Sn[%s]", Sn);
		return -1;
	}
	/* 如果不存在该SN号的链路，查询是否还有链路空间 */
	if (blePetSearchIdleServer() < 0)
	{
		LogPrintf(DEBUG_BLE, "blePetServerAdd==>No idle server");
		return -2;
	}
	int8_t i; 
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (blePetServConn[i].use == 0)
		{
			blePetServConn[i].use = 1;
			memcpy(blePetServConn[i].loginSn, Sn, 15);
			blePetServConn[i].loginSn[15] = 0;
			LogPrintf(DEBUG_BLE, "blePetServerAdd==>Ok,Sn[%s],sockid:%d", blePetServConn[i].loginSn, BleSockId(i));
			return BleSockId(i);
		}
	}
	return -1;
}

/**************************************************
@bref		删除蓝牙链路
@param
@return
@note
**************************************************/

int8_t blePetServerDel(char *Sn)
{
	/* 无该sn号 */
	if (blePetSearchServerSn(Sn) < 0)
	{
		LogPrintf(DEBUG_BLE, "blePetServerDel==>No this Sn[%s]", Sn);
		return -1;
	}
	for (uint8_t i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (blePetServConn[i].use)
		{
			if (strncmp(Sn, blePetServConn[i].loginSn, 15) == 0)
			{
				LogPrintf(DEBUG_BLE, "blePetServerDel==>Ok,Sn[%s],sockid:%d", blePetServConn[i].loginSn, BleSockId(i));
				tmos_memset(&blePetServConn[i], 0, sizeof(bleSocketInfo_s));
				return BleSockId(i);
			}
		}
	}
	return -2;
}


/**************************************************
@bref		更新蓝牙链路的上报信息
@param
@return
@note
	更新上报信息之前，先查看上报信息对应的sn号是否存在
**************************************************/
int8_t blePetServerUploadUpdate(devSocketData_s *data)
{
	/* 该链路的SN号不是这次收到，不更新 */
	if (blePetSearchServerSn(data->SN) < 0)
	{
		LogPrintf(DEBUG_BLE, "blePetServerUploadUpdate==>No exist sn[%s]", data->SN);
		return -1;
	}
	
	blePetServConn[blePetSearchServerSn(data->SN)].batlevel = data->bat;
	blePetServConn[blePetSearchServerSn(data->SN)].vol      = data->vol;
	blePetServConn[blePetSearchServerSn(data->SN)].step		= data->step;
	LogPrintf(DEBUG_BLE, "Dev(%d) sock(%d)InfoRegister:vol:%.1f, bat:%d%%, step:%d", 
							blePetSearchServerSn(data->SN), 
							BleSockId(blePetSearchServerSn(data->SN)),
							blePetServConn[blePetSearchServerSn(data->SN)].vol,
							blePetServConn[blePetSearchServerSn(data->SN)].batlevel,
							blePetServConn[blePetSearchServerSn(data->SN)].step);
	return blePetSearchServerSn(data->SN);
}

static void ble1PetServerSocketRecv(char *data, uint16_t len)
{
    uint16_t i, beginindex, contentlen, lastindex;
    //遍历，寻找协议头
    for (i = 0; i < len; i++)
    {
        beginindex = i;
        if (data[i] == 0x78)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x78)
            {
                continue ;
            }
            if (i + 2 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2];
            if ((i + 5 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 3 + contentlen] == 0x0D && data[i + 4 + contentlen] == 0x0A)
            {
                i += (4 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7878[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(BLE1_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
        else if (data[i] == 0x79)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x79)
            {
                continue ;
            }
            if (i + 3 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2] << 8 | data[i + 3];
            if ((i + 6 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 4 + contentlen] == 0x0D && data[i + 5 + contentlen] == 0x0A)
            {
                i += (5 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7979[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(BLE1_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
    }
}

static void ble2PetServerSocketRecv(char *data, uint16_t len)
{
    uint16_t i, beginindex, contentlen, lastindex;
    //遍历，寻找协议头
    for (i = 0; i < len; i++)
    {
        beginindex = i;
        if (data[i] == 0x78)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x78)
            {
                continue ;
            }
            if (i + 2 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2];
            if ((i + 5 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 3 + contentlen] == 0x0D && data[i + 4 + contentlen] == 0x0A)
            {
                i += (4 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7878[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(BLE2_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
        else if (data[i] == 0x79)
        {
            if (i + 1 >= len)
            {
                continue ;
            }
            if (data[i + 1] != 0x79)
            {
                continue ;
            }
            if (i + 3 >= len)
            {
                continue ;
            }
            contentlen = data[i + 2] << 8 | data[i + 3];
            if ((i + 6 + contentlen) > len)
            {
                continue ;
            }
            if (data[i + 4 + contentlen] == 0x0D && data[i + 5 + contentlen] == 0x0A)
            {
                i += (5 + contentlen);
                lastindex = i + 1;
                //LogPrintf(DEBUG_ALL, "Fint it ====>Begin:7979[%d,%d]", beginindex, lastindex - beginindex);
                protocolRxParser(BLE2_LINK, (char *)data + beginindex, lastindex - beginindex);
            }
        }
    }
}

bleSocketRecvCb_t BlePetSocketRecv(uint8_t i)
{
	if (i == 0)
	{
		return ble1PetServerSocketRecv;
	}
	else if (i == 1)
	{
		return ble2PetServerSocketRecv;
	}
	else 
	{
		return ble2PetServerSocketRecv;
	}
}


/**************************************************
@bref		蓝牙双链路的状态切换
@param
@return
@note
**************************************************/

static void blePetChangeFsm(uint8_t index, NetWorkFsmState fsm)
{
	blePetServConn[index].fsmstate = fsm;
	LogPrintf(DEBUG_BLE, "blePetChangeFsm(%d)==>%d", index, blePetServConn[index].fsmstate);
}

/**************************************************
@bref		主服务器登录正常
@param
@return
@note
**************************************************/

void blePetServerLoginSuccess(uint8_t index)
{
	deviceConnInfo_s *devinfo = bleDevGetInfoBySockid(BleSockId(index));
    blePetServConn[index].loginCount = 0;
    blePetServConn[index].heartbeattick = 0;
    blePetChangeFsm(index, SERV_READY);
    LogPrintf(DEBUG_BLE, "Ble[%d] Login Success", BleSockId(index));
}

/**************************************************
@bref		蓝牙双链路的管理
@param
@return
@note
**************************************************/

void blePetServerConnTask(void)
{
	uint8_t i, link; 
	if (isModuleRunNormal() == 0)
    {
        LogPrintf(DEBUG_BLE, "wait module normal");
        return ;
    }
	for (i = 0; i < DEVICE_MAX_CONNECT_COUNT; i++)
	{
		if (blePetServConn[i].use == 0)
		{
			if (socketGetUsedFlag(BleSockId(i)) == 1)
			{
				socketDel(BleSockId(i));
			}
			continue;
		}
		if (socketGetUsedFlag(BleSockId(i)) == 0)
		{
			blePetChangeFsm(i, SERV_LOGIN);
			blePetServConn[i].loginCount = 0;
			socketAdd(BleSockId(i), sysparam.bleServer, sysparam.bleServerPort, BlePetSocketRecv(i));
			continue;
		}
		
		if (socketGetConnStatus(BleSockId(i)) != SOCKET_CONN_SUCCESS)
		{
			LogPrintf(DEBUG_BLE, "wait blelink:%d server ready", BleSockId(i));
			blePetChangeFsm(i, SERV_LOGIN);
			continue;
		}
		switch (blePetServConn[i].fsmstate)
		{
			case SERV_LOGIN:
			    protocolSnRegister(blePetServConn[i].loginSn);
	            protocolSend(BleSockId(i), PROTOCOL_01, NULL);
	            blePetChangeFsm(i, SERV_LOGIN_WAIT);
				break;
			case SERV_LOGIN_WAIT:
				blePetServConn[i].logintick++;
	            if (blePetServConn[i].logintick >= 30)
	            {
	                blePetChangeFsm(i, SERV_LOGIN);
	                blePetServConn[i].loginCount++;
	                if (blePetServConn[i].loginCount >= 2)
	                {
	                    blePetChangeFsm(i, SERV_LOGIN);
	                }
	            }
				break;
			case SERV_READY:
	            if (blePetServConn[i].heartbeattick % sysparam.heartbeatgap == 0)
	            {
	                blePetServConn[i].heartbeattick = 0;
	                protocolInfoResiter(blePetServConn[i].batlevel, sysinfo.outsidevoltage > 5.0 ? sysinfo.outsidevoltage : blePetServConn[i].vol,
	                                    dynamicParam.startUpCnt, blePetServConn[i].step);
	                protocolSend(BleSockId(i), PROTOCOL_13, NULL);
	                protocolSend(BleSockId(i), PROTOCOL_12, getLastFixedGPSInfo());
	            }
	            blePetServConn[i].heartbeattick++;
	            break;
	        default:
				blePetChangeFsm(i, SERV_LOGIN);
				blePetServConn[i].logintick = 0;
	        	break;
		}
	}

}

bleSocketInfo_s *getBlepetServerInfo(void)
{
	return blePetServConn;
}


/**************************************************
@bref		agps请求
@param
	none
@return
	none
@note
**************************************************/

void agpsRequestSet(void)
{
    sysinfo.agpsRequest = 1;
    LogMessage(DEBUG_ALL, "agpsRequestSet==>OK");
}

void agpsRequestClear(void)
{
    sysinfo.agpsRequest = 0;
    LogMessage(DEBUG_ALL, "agpsRequestClear==>OK");
}

/**************************************************
@bref		socket数据接收
@param
@return
@note
**************************************************/

static void agpsSocketRecv(char *data, uint16_t len)
{
    LogPrintf(DEBUG_ALL, "Agps Reject %d Bytes", len);
    //LogMessageWL(DEBUG_ALL, data, len);
    portUartSend(&usart3_ctl, data, len);
}



static void agpsServerConnTask(void)
{
    static uint8_t agpsFsm = 0;
    static uint8_t runTick = 0;
    char agpsBuff[150] = {0};
    uint16_t agpsLen;
    int ret;
    gpsinfo_s *gpsinfo;

    if (sysparam.agpsen == 0)
    {
		sysinfo.agpsRequest = 0;
		agpsFsm = 0;
    	if (socketGetUsedFlag(AGPS_LINK))
    	{
			socketDel(AGPS_LINK);
    	}
		return;
    }
    if (sysinfo.agpsRequest == 0)
    {
    	agpsFsm = 0;
    	if (socketGetUsedFlag(AGPS_LINK))
    	{
			socketDel(AGPS_LINK);
    	}
        return;
    }

    gpsinfo = getCurrentGPSInfo();

    if (isModuleRunNormal() == 0)
    {
        return ;
    }
    if (gpsinfo->fixstatus || sysinfo.gpsOnoff == 0)
    {
        socketDel(AGPS_LINK);
        agpsRequestClear();
        return;
    }
    if (socketGetUsedFlag(AGPS_LINK) != 1)
    {
        agpsFsm = 0;
        ret = socketAdd(AGPS_LINK, sysparam.agpsServer, sysparam.agpsPort, agpsSocketRecv);
        if (ret != 1)
        {
            LogPrintf(DEBUG_ALL, "agps add socket err[%d]", ret);
            agpsRequestClear();
        }
        return;
    }
    if (socketGetConnStatus(AGPS_LINK) != SOCKET_CONN_SUCCESS)
    {
    	agpsFsm = 0;
        LogMessage(DEBUG_ALL, "wait agps server ready");
        return;
    }
    switch (agpsFsm)
    {
        case 0:
//            if (gpsinfo->fixstatus == 0)
//            {
//                sprintf(agpsBuff, "user=%s;pwd=%s;cmd=full;", sysparam.agpsUser, sysparam.agpsPswd);
//                socketSendData(AGPS_LINK, (uint8_t *) agpsBuff, strlen(agpsBuff));
//            }
			createProtocolA0(agpsBuff, &agpsLen);
			socketSendData(AGPS_LINK, (uint8_t *) agpsBuff, agpsLen);
            agpsFsm = 1;
            runTick = 0;
            break;
        case 1:
            if (++runTick >= 30)
            {
            	if (isAgpsDataRecvComplete() == 0)
            	{
	                agpsFsm = 0;
	                runTick = 0;
	                socketDel(AGPS_LINK);
	                wifiRequestSet(DEV_EXTEND_OF_MY);
	                agpsRequestClear();
                }
            }
            break;
        default:
            agpsFsm = 0;
            break;
    }


}

/**************************************************
@bref		服务器链接管理任务
@param
@return
@note
**************************************************/

void serverManageTask(void)
{
    if (sysparam.protocol == JT808_PROTOCOL_TYPE)
    {
        jt808ServerConnTask();
        //bleJt808ServerConnTask();
    }
    else
    {
        privateServerConnTask();
        //bleServerConnTask();
    }   
    blePetServerConnTask();
    hiddenServerConnTask();
    agpsServerConnTask();
}

/**************************************************
@bref		判断主要服务器是否登录正常
@param
@return
@note
**************************************************/

uint8_t primaryServerIsReady(void)
{
    if (isModuleRunNormal() == 0)
        return 0;
    if (sysparam.protocol == JT808_PROTOCOL_TYPE)
    {
        if (socketGetConnStatus(JT808_LINK) != SOCKET_CONN_SUCCESS)
            return 0;
        if (jt808ServConn.connectFsm != JT808_NORMAL)
            return 0;
    }
    else
    {
        if (socketGetConnStatus(NORMAL_LINK) != SOCKET_CONN_SUCCESS)
            return 0;
        if (privateServConn.fsmstate != SERV_READY)
            return 0;
    }
    return 1;
}

/**************************************************
@bref		判断隐藏服务器是否登录正常
@param
@return
@note
**************************************************/

uint8_t hiddenServerIsReady(void)
{
    if (isModuleRunNormal() == 0)
        return 0;
    if (socketGetConnStatus(HIDDEN_LINK) != SOCKET_CONN_SUCCESS)
        return 0;
    if (hiddenServConn.fsmstate != SERV_READY)
        return 0;
    return 1;
}

