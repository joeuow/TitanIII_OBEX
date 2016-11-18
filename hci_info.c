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
* File Name:			hci_info.c
* Last Modified:
* Changes:
**********************************************************************/

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <debuglog.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "ositech_bt.h"
#include "ositech_communication.h"
#include "config.h"
#include "hci_info.h"

#define HCI_INFO_STRING_SIZE	64
#define HCI_INFO_FILENAME_SIZE	64
/*********************************************************************** 
* Description:
* Open a Bluetooth HCI socket.
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		socket id
******************************************************************************/
static int OpenHCISocket(void) {
	int ctl;
	
	if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
		perror("Can't open HCI socket.");
		return 0;
	}

	return ctl;
}
	
/*********************************************************************** 
* Description:
* Close a Bluetooth HCI socket.
* 
* Calling Arguments: 
* Name		Description 
* int		socket id
*
* Return Value: 
* None
******************************************************************************/
static void CloseHCISocket(const int ctl) {
	close(ctl);
}

/*********************************************************************** 
* Description:
* Get the HCI device information
* 
* Calling Arguments: 
* Name			Description 
* ctl		the device file descriptor
* di		the varible to store the get device information
*
* Return Value: 
* 0:		error
* 1: 		success
******************************************************************************/
static int GetHCIDevInfo(const int ctl, struct hci_dev_info *di) {
	int i;
	struct hci_dev_list_req *dl;
	struct hci_dev_req *dr;

	if (!(dl = malloc(HCI_MAX_DEV * sizeof(struct hci_dev_req) + sizeof(uint16_t)))) {
		perror("Can't allocate memory");
		return 0;
	}
	memset(dl, 0, BT_DEV_NUM*sizeof(struct hci_dev_req) + sizeof(uint16_t));
	dl->dev_num = BT_DEV_NUM;
	dr = dl->dev_req;
	
	if (ioctl(ctl, HCIGETDEVLIST, (void *) dl) < 0) {
		perror("Can't get device list");
		free(dl);
		return 0;
	}

	for (i = 0; i< dl->dev_num; i++) {
		di->dev_id = (dr+i)->dev_id;
		if (ioctl(ctl, HCIGETDEVINFO, (void *) di) < 0) {
			continue;
		}
	}

	return 1;
}

/*********************************************************************** 
* Description:
* Check if the bluetooth device is discoverable or not
* 
* Calling Arguments: 
* Name			Description 
* none
*
* Return Value: 
* 0: 		not discoverable
* 1: 		discoverable
******************************************************************************/
int GetBTDevDiscov(void) {
	int ctl, disc = 0;
	struct hci_dev_info di;

	if((ctl = OpenHCISocket())) {	
		if (GetHCIDevInfo(ctl, &di)) {
		//	if(di.flags & BT_ISCAN_BIT || di.flags & BT_PSCAN_BIT)
			if(di.flags & BT_ISCAN_BIT)
				disc = 1;
			else
				disc = 0;
		}
		CloseHCISocket(ctl);
	}

	return disc;
}
/*********************************************************************** 
* Description:
* Get the path of the configuration file regarding to the HCI device
* 
* Calling Arguments: 
* Name			Description 
* fullpath			the pathname return
* filename		the name of the file searching for
*
* Return Value: 
* void
******************************************************************************/
void GetBTFilePath(char *fullpath, const char *filename) {
	struct hci_dev_info di;
	char btaddr_string[HCI_INFO_STRING_SIZE] = {};
	int ctl, ret;
	
	if((ctl = OpenHCISocket())) {
		ret = GetHCIDevInfo(ctl, &di);
		ba2str(&di.bdaddr, btaddr_string);
	//	if (ret) 
	//		printf("HCI device: %s\n", btaddr_string);

		CloseHCISocket(ctl);
	}
	
	snprintf(fullpath, HCI_INFO_FILENAME_SIZE*sizeof(char), "%s/%s/%s", STORAGEDIR, btaddr_string, filename);
}

/*********************************************************************** 
* Description:
* Get the dev_id of the bluetooth adaptor
* 
* Calling Arguments: 
* Name			Description 
* none
*
* Return Value: 
* -1:	error
* else: 	dev_id
******************************************************************************/
int GetBTDevID(void) {
	int ctl;
	int ret = -1;
	struct hci_dev_info di;

	if((ctl = OpenHCISocket())) {
		ret = GetHCIDevInfo(ctl, &di);
		
		if (ret) {
		//	printf("HCI device: %s\n", di.name);
			ret = di.dev_id;
		}
		CloseHCISocket(ctl);
	}

	return ret;
}

int GetBTDevAdd(bdaddr_t *addr) {
	int ctl,  ret= 0;
	struct hci_dev_info di;

	if((ctl = OpenHCISocket())) {	
		ret = GetHCIDevInfo(ctl, &di);
		if (ret) {
			bacpy(addr, &di.bdaddr);	
		}
		CloseHCISocket(ctl);
	}

	return ret;
}
/*********************************************************************** 
* Description:
* open the bluetooth configuration file
* 
* Calling Arguments: 
* Name			Description 
* filename		the name of the file 
*
* Return Value: 
* FILE		the file stream of the open file
******************************************************************************/
FILE *OpenFile(const char *filename, const char *mode) {
	char fullpath[HCI_INFO_FILENAME_SIZE] = {};

	GetBTFilePath(fullpath, filename);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: fopen() file %s\n", __FUNCTION__, fullpath);
	
	return fopen(fullpath, mode);
}

/*********************************************************************** 
* Description:
* close the bluetooth configuration file
* 
* Calling Arguments: 
* Name			Description 
* FILE		the file stream of the open file
*
* Return Value: 
* none
******************************************************************************/
void CloseFile(FILE *filename) {
	fclose(filename);
}