/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source 
 * code package.
 */
 
  /*********************************************************************** 
* Original Author: 		Joe Wei
* File Creation Date: 	Sep/22/2016
* Project: 			ositech_obex
* Description: 		
* File Name:			hci_info.h
* Last Modified:
* Changes:
**********************************************************************/
#ifndef __HCI_INFO_H
#define __HCI_INFO_H

#define BT_DEV_NUM	1

#define BT_PSCAN_BIT	0x8
#define BT_ISCAN_BIT	0x10

#define STORAGEDIR	"/mnt/flash/titan-data/bluetooth"

extern FILE *OpenFile(const char *filename, const char *mode);
extern void CloseFile(FILE *filename);
extern int GetBTDevID(void);
extern void GetBTFilePath(char *fullpath, const char *filename);
extern int GetBTDevDiscov(void);
extern int GetBTDevAdd(bdaddr_t *addr);

#endif