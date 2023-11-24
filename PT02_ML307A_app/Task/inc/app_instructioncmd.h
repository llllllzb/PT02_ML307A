#ifndef APP_INSTRUCTION_H
#define APP_INSTRUCTION_H

#include <stdint.h>
#include "app_sys.h"

typedef enum{
	DEBUG_MODE,
	SMS_MODE,
	NET_MODE,
	BLE_MODE,
	JT808_MODE,
}insMode_e;

typedef enum{
	PARAM_INS,
	STATUS_INS,
	VERSION_INS,
	SN_INS,
	SERVER_INS,
	MODE_INS,
	HBT_INS,
	POSITION_INS,
	APN_INS,
	UPS_INS,
	LOWW_INS,
	LED_INS,
	POITYPE_INS,
	RESET_INS,
	UTC_INS,
	DEBUG_INS,
	ACCCTLGNSS_INS,
	ACCDETMODE_INS,
	FENCE_INS,
	FACTORY_INS,
	ICCID_INS,
	SETAGPS_INS,
	JT808SN_INS,
	HIDESERVER_INS,
	BLESERVER_INS,
    BF_INS,
    CF_INS,
    PROTECTVOL_INS,
    TIMER_INS,
    QGMR_INS,
    MOTIONDET_INS,
    FCG_INS,
    QFOTA_INS,
    BLEEN_INS,
    AGPSEN_INS,
    SETBATRATE_INS,
    SETMILE_INS,
    SETPETMAC_INS,
    PETDEBUG_INS,
}INSTRUCTIONID;



typedef struct
{
	uint16_t cmdid;
	char *   cmdstr;
}instruction_s;

typedef struct{
	char * telNum;
	char * data;
	uint16_t len;
	uint8_t link;
}insParam_s;

extern insParam_s lastparam;
void instructionRespone(char *message);
void instructionParser(uint8_t *str, uint16_t len, insMode_e mode, void * param);
void dorequestSend123(void);
int16_t getInstructionid(uint8_t *cmdstr);
void doinstruction(int16_t cmdid, ITEM *item, insMode_e mode, void *param);
#endif
