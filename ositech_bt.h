/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source 
 * code package.
 */
 
  /*********************************************************************** 
* Original Author: 		Joe Wei
* File Creation Date: 	May/22/2016
* Project: 			ositech_obex
* Description: 		
* File Name:			ositech_bt.h
* Last Modified:
* Changes:
**********************************************************************/

#ifndef __OSITECH_BT_H
#define __OSITECH_BT_H

#define BT_INQ_LIST_DEV_NUM	8
#define RETRY_TIMES	3
#define BT_ADDR_LENGTH		18
#define BT_NAME_LENGTH		248
#define PINCODE_FILE	"/tmp/BT_pincode"


#define BT_LED_OFF			0
#define BT_LED_SOLID		1
#define BT_LED_FLASH_DISCOVERABLE	2
#define BT_LED_FLASH_INQ	3
#define BT_LED_DATA_ACTIVITY	4

#define MASTER_ROLE		0x0
#define SLAVE_ROLE	0x1

extern int BTSetName(const char *name);
extern int BTGetInq(const int cli);
extern int BTSetPIN(const char *pin_code);
extern int BTInitPair(const char *arg, int *start_pair);
extern void GetTrustList(const int sockfd);
extern int RmTrustDev(const char *addr);
extern void RmAllTrustDev(void);
extern int ValidRegisters(const char *pregister);
extern int ValidName(const char *name);
extern int ValidPin(const char *code);
extern int GetCurBTLed(void);
extern void SetBTLed(const int mode);
extern int ParseATDArg(const char *arg, char *addr);
extern void StoreName(const char *pname);
extern int BTLoadName(char **pname);
extern void DelNameFile(void);
extern int SearchPairedDev(const char *addr);
extern char *GetPairingDeviceName(const char *addr);
extern void UpdatePairedDevice(const char *addr, const char *string);

#endif