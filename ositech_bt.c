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
* File Name:			ositech_bt.c
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

#include <bluetooth/l2cap.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include "ositech_bt.h"
#include "ositech_communication.h"
#include "config.h"
#include "hci_info.h"

#define FILENAME_SIZE	64

#define BUFF_SIZE	128

static void BTFindandDel(const char *filename, const char *addr, const char *new_string);
static int BTFindandReplace(const char *filename, const char *addr, const char *new_string);
/*********************************************************************** 
* Description:
* Open the Bluetootth L2CAP socket.
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		socket id
******************************************************************************/
static int OpenL2capSocket(void) {
	int sock_fd;

	sock_fd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_L2CAP);
	
	if (sock_fd < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: socket() failed and Can't open L2CAP control socket\n", __FUNCTION__);
		perror("Can't open L2CAP control socket");
		return -1;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: socket() SUCCESS\n", __FUNCTION__);

	return sock_fd;
}

static void SetL2cap(struct sockaddr_l2 *laddr, struct sockaddr_l2 *raddr, const char* arg, const uint psm) {
	laddr->l2_family = AF_BLUETOOTH;
	bacpy(&laddr->l2_bdaddr, BDADDR_ANY);
	laddr->l2_psm = 0;
	
	raddr->l2_family = AF_BLUETOOTH;
	str2ba(arg, &raddr->l2_bdaddr);
	raddr->l2_psm = htobs(psm);
}

/*********************************************************************** 
* Description:
* Close a Bluetooth L2CAP socket.
* 
* Calling Arguments: 
* Name		Description 
* int		socket id
*
* Return Value: 
* None
******************************************************************************/
static void CloseL2capSocket(const int ctl) {
	close(ctl);
}

/*********************************************************************** 
* Description:
* valid the given address to see if it's correct in format
* 
* Calling Arguments: 
* Name			Description 
* addr		the given bluetoothe device address
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
static int ValidAddr(const char *addr) {
	int i = 0;
	
	if(strlen(addr) != 17) {
		printf("%d beyond 17\n", strlen(addr));
		return 0;
	}

	while(i < strlen(addr)) {
		if ((*(addr + i) <= '9' && *(addr + i) >= '0') ||
			( *(addr + i) >= 'A' && *(addr + i) <= 'F') ||
			( *(addr + i) >= 'a' && *(addr + i) <= 'f') ||
			( *(addr + i) >= ':'))
			i++;
		else {
			printf("%c is invalid\n", *(addr + i));
			return 0;
		}
	}

	return 1;
	
}


/*********************************************************************** 
* Description:
* Check if the give address is matched with the reading
* 
* Calling Arguments: 
* Name			Description 
* reading		the readings from the file
* addr		the given bluetoothe device address
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
// both in xx:xx:xx:xx:xx format
static int FindAddr(char *reading, const char *addr) {	
	char string[BUFF_SIZE] = {};
	const char dlim[] = ",: ";
	char *ptok;
	int addr_index = 0;
	int addr_len = strlen(addr);

	snprintf(string, BUFF_SIZE*sizeof(char), "%s", reading);
	ptok = strtok(string, dlim);
	//printf("%s - %s\n", ptok, addr+addr_index);
	if(!strncmp(ptok, addr, 2)) { // compare first 2 value of the address
		addr_index += strlen(ptok)+1;	// strlen("xx:")
		while((ptok = strtok(NULL, dlim))) {
		//	printf("%s - %s\n", ptok, addr+addr_index);
			if(!strncmp(ptok, addr+addr_index, 2)) {
				if(addr_index+strlen(ptok)+1 >= addr_len)
					break;	
				else 
					addr_index += strlen(ptok)+1;
			} else
				return 0;	
		}
	} else
		return 0;

	return 1;
}

/*********************************************************************** 
* Description:
* Get the current state of the bluetooth led
* 
* Calling Arguments: 
* Name			Description 
* none
*
* Return Value: 
* BT_LED_FLASH_DISC:	led is flash in discovery mode
* BT_LED_OFF: 	led is off.
******************************************************************************/
int GetCurBTLed(void) {
	int dis = GetBTDevDiscov();

	if (dis) 
		return BT_LED_FLASH_DISCOVERABLE;
	else
		return BT_LED_OFF;
}

/*********************************************************************** 
* Description:
* Set the Bluetooth LED operation
* 
* Calling Arguments: 
* Name			Description 
* int		LED operation mode
*
* Return Value: 
* none
******************************************************************************/
void SetBTLed(const int mode) {
	char led_cmd[32] = "titan3_led bt";

	snprintf(led_cmd+strlen(led_cmd), sizeof(led_cmd)-strlen(led_cmd), " %d", mode);
//	printf("%s\n", led_cmd);
	system(led_cmd);
}

/*********************************************************************** 
* Description:
* validate the given name, pin code, or the register is corret 
* 
* Calling Arguments: 
* Name			Description 
* nane/code/pregister
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
int ValidName(const char *name) {
	int name_leng = strlen(name);

	if (name_leng < 0 || name_leng > 247) // max length is 247
		return 0;
	else 
		return 1;
}

int ValidPin(const char *code) {
	int code_leng = strlen(code);

	if (code_leng < 0 || code_leng > 8) // max length is 8
		return 0;
	else 
		return 1;
}

int ValidRegisters(const char *pregister) {
	char *pequal = NULL;
	int index = 0;
	
	// format of the register string should be N[N[N]]=N[N[N]]
	if (strlen(pregister) > 7)
		return 0;
	if (!(pequal = strchr(pregister, '=')))
		return 0;
	if ((pequal - pregister) > 3 ||
		(pequal - pregister) <= 0 ||
		strlen(pequal+1) > 3 || 
		strlen(pequal+1) <= 0)
		return 0;
	
	while(index < strlen(pregister)) {
		if (*(pregister + index) == '=') {
			index++;
		} else {
			if (*(pregister + index) > '9' ||*(pregister + index) < '0' ) return 0;
			else
				index++;
		}
	}

	return 1;
}

// -1: invalid option of uuid
// 0 : success
int ParseATDArg(const char *arg, char *addr) {
	char *pcoma = NULL;
	uint8_t arg_leng = strlen(arg);
	uint8_t addr_leng = 0;
	uint8_t start_pos = 0;

	// UYaaaabbbbcccc,<uuid>
	if((pcoma = strchr(arg, ','))) {
		if(strncmp(pcoma+1, "1106", strlen("1106")))
			return -1;
		else
			addr_leng = pcoma - arg;
	} else
		addr_leng = arg_leng;

	if(addr_leng > 12)
		start_pos = addr_leng - 12;

	snprintf(addr, 13, "%s", arg+start_pos);
	return 0;
}

/*********************************************************************** 
* Description:
* Set friendly name of the bluetooth adaptor
* 
* Calling Arguments: 
* Name			Description 
* name		the given friendly name of the bluetooth adpator
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
int BTSetName(const char *name) 
{
	int dd;
	int hdev;
	int error;
	
	if((hdev = GetBTDevID()) < 0) {
		perror("Error: Get Bluetooth Device failed.");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s Get Bluetooth Device failed.\n", __FUNCTION__);
		return 0;
	}
	
	if((dd = hci_open_dev(hdev)) < 0) {
		error = errno;
		fprintf(stderr, "Can't open device hci%d: %s (%d)\n",
						hdev, strerror(error), error);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: Can't open device hci%d: %s (%d)\n",
						hdev, strerror(error), error);
		return 0;
	}

	if (hci_write_local_name(dd, name, 2000) < 0) {
		error = errno;
		fprintf(stderr, "Can't change local name on hci%d: %s (%d)\n",
						hdev, strerror(errno), errno);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: Can't change local name on hci%d: %s (%d)\n",
						hdev, strerror(error), error);
		return 0;
	} 

	hci_close_dev(dd);

	return 1;
}

// -1: error
// 0: no name is set
// 1: load name successfully
int BTLoadName(char **pname) {
	FILE *file_stream;
	int sz;
	
	file_stream = OpenFile("friendlyname", "r");
	if (file_stream) {
		fseek(file_stream, 0L, SEEK_END);
		sz = ftell(file_stream);
		fseek(file_stream, 0L, SEEK_SET);
	} else {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: friendly name is not set\n", __FUNCTION__);
		return 0;
	}
	
	if (sz) {
		*pname = (char *)malloc((sz+1)*sizeof(char));
		if (!*pname) {
			perror("BTLoadName(): malloc()");
			return -1;
		}
	}
	fgets(*pname, (sz+1)*sizeof(char), file_stream);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: friendly name is %s\n", __FUNCTION__, *pname);
	CloseFile(file_stream);
	
	return 1;
}

// store the friendly name
void StoreName(const char *pname) {
	FILE *file_stream;
	
	file_stream = OpenFile("friendlyname", "w");
	fwrite(pname, sizeof(char), strlen(pname), file_stream);
	CloseFile(file_stream);
}

void DelNameFile(void) {
	char fullpath[FILENAME_SIZE] = {};

	GetBTFilePath(fullpath, "friendlyname");
	if(access(fullpath, F_OK) < 0)
		printf("%s not existed\n", fullpath);
	else 
		unlink(fullpath);
}

/*********************************************************************** 
* Description:
* get the inquery result of the bluetooth adaptor
* 
* Calling Arguments: 
* Name			Description 
* cli		device id of the bluetooth adaptor
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
int BTGetInq(const int cli)
{
	inquiry_info *info = NULL;
	uint8_t lap[3] = { 0x33, 0x8b, 0x9e };
	int dev_id, dd;
	int i, num_rsp;
	int ret;
	char addr[18];
	char inq_res[128];
	char dev_name[64];

	printf("Inquiring ...\n");

	if((dev_id = GetBTDevID()) < 0) {
		perror("Error: Get Bluetooth Device failed.");
		return 0;
	}
	
	num_rsp = hci_inquiry(dev_id, BT_INQ_LIST_DEV_NUM, 0, lap, &info, IREQ_CACHE_FLUSH);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_inquiry() find %d BT devs\n", __FUNCTION__, num_rsp);
	if (num_rsp < 0) {
		perror("Inquiry failed.");
		return 0;
	}
	
	if((dd = hci_open_dev(dev_id)) < 0) {
		perror("HCI device open failed");
		goto end;
	}

	for (i = 0; i < num_rsp; i++) {
		memset(inq_res, 0, sizeof(inq_res));
		memset(dev_name, 0, sizeof(dev_name));
		ba2str(&(info+i)->bdaddr, addr);
		AddrStringRmColumn(addr);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: find BT #%d: %s\n", __FUNCTION__, i, addr);

		snprintf(inq_res, sizeof(inq_res), "%s,%2.2x%2.2x%2.2x,",
			addr,
			(info+i)->dev_class[2],
			(info+i)->dev_class[1],
			(info+i)->dev_class[0]);
		
		if (hci_read_remote_name(dd, &(info+i)->bdaddr, sizeof(dev_name), dev_name, 25000) == 0)
			snprintf(inq_res+strlen(inq_res), sizeof(inq_res) - strlen(inq_res), "\"%s\"", dev_name);
		else {
			snprintf(inq_res+strlen(inq_res), sizeof(inq_res) - strlen(inq_res), "\"*%s\"", addr);
		}
		printf("%s\n", inq_res);
		SendResponse(cli, inq_res);
	}
	
	hci_close_dev(dd);
	ret = 1;
	
end:
	bt_free(info);	
	return ret;
}

/*********************************************************************** 
* Description:
* save the pin code of pairing
* 
* Calling Arguments: 
* Name			Description 
* pin_code	pin code string
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
int BTSetPIN(const char *pin_code) {
	int fd;
	int wr_sz;
	unsigned int retry = 0;
	int pin_code_leng = strlen(pin_code);
	
	if(!pin_code)
		return 0;

	printf("PIN code: %s\n", pin_code);
	if((fd = open(PINCODE_FILE, O_WRONLY | O_CREAT)) < 0) {
		printf("Error: %s(%d) open()\n", __FUNCTION__, __LINE__);
		return 0;
	}

	do {
		printf("Saving the PIN code...\n");
		if((wr_sz = write(fd, pin_code, pin_code_leng)) < 0) {
			printf("Error: %s(%d) write()\n", __FUNCTION__, __LINE__);
			return 0;
		}

		if (retry++ >= RETRY_TIMES)
			break;
		
	} while(wr_sz < pin_code_leng);	//wr_sz < pin_code length, rewrite

	return 1;
}

static void PageScan(int dev_id, const int scan_switch) {
	int ctl;
	struct hci_dev_req dr;

	if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: socket() failed\n", __FUNCTION__);
		perror("Can't open HCI socket.");
		return;
	}
	
	dr.dev_id  = dev_id;

	if(scan_switch == 0)
		dr.dev_opt = SCAN_DISABLED;
	else if (scan_switch)
		dr.dev_opt = SCAN_PAGE;
		

	if (ioctl(ctl, HCISETSCAN, (unsigned long) &dr) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: ioctl(HCISETSCAN) failed\n", __FUNCTION__);
		perror("HCI ioctl failed.");
	}

	close(ctl);
}
/*********************************************************************** 
* Description:
* get the inquery result of the bluetooth adaptor
* 
* Calling Arguments: 
* Name			Description 
* arg	remote bluetooth device address
* start_pait 	state of pairing
*
* Return Value: 
* 0: success
* 1: timeout
* 2: otherwise
******************************************************************************/
int BTInitPair(const char *arg, int *start_pair) {
	bdaddr_t bdaddr;
	int dd, dev_id;
	uint16_t handle;
	unsigned int ptype = HCI_DM1 | HCI_DM3 | HCI_DM5 | HCI_DH1 | HCI_DH3 | HCI_DH5;

	if(!arg) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: pairing BT device address is not given\n", __FUNCTION__);
		return 0;
	}
	printf("Pairing to %s...\n", arg);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Pairing to %s....\n", __FUNCTION__, arg);
	if(!ValidAddr(arg)) {
		*start_pair = 0;
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: pairing BT device address %s is invalid (code 2)\n", __FUNCTION__, arg);
		return 2;
	} else {
		*start_pair = 1;
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: BT device address %s is Valid.\n", __FUNCTION__, arg);
	}
	
	str2ba(arg, &bdaddr);

	dev_id = hci_get_route(&bdaddr);
	if (dev_id < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_get_route() local BT device Failed\n", __FUNCTION__);
		return 2;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_get_route() local BT device SUCCESS\n", __FUNCTION__);

	// enable page scan 
//	PageScan(dev_id, 1);

	dd = hci_open_dev(dev_id);
	if (dd < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_open_dev() local BT device Failed\n", __FUNCTION__);
		return 2;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_open_dev() local BT device SUCCESS\n", __FUNCTION__);

	
	if (hci_create_connection(dd, &bdaddr, htobs(ptype), htobs(0x0000), SLAVE_ROLE, &handle, 25000) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_create_connection() remote BT device Failed\n", __FUNCTION__);
		return 2;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_create_connection() remote BT device SUCCESS\n", __FUNCTION__);

	// disable page scan
//	sleep(10);
//	PageScan(dev_id, 0);
	
	hci_close_dev(dd);

	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s returns success\n", __FUNCTION__);
	return 0;
}

/*********************************************************************** 
* Description:
* get the list of trusted devices (paired devices)
* 
* Calling Arguments: 
* Name			Description 
* sockfd		the socket id of the socket used to return the result 
*
* Return Value: 
*none
******************************************************************************/
void GetTrustList(const int sockfd) {
	FILE *linkkey_file_stream, *paireddev_file_stream;
	char linkkey_read_buff[BUFF_SIZE] = {};
	char paireddev_read_buff[BUFF_SIZE] = {};
	
	char read_bt_add[BT_ADDR_LENGTH] = {};
	char read_bt_name[BT_NAME_LENGTH] = {};
	char resp_string[BT_ADDR_LENGTH+BT_NAME_LENGTH] = {};
	int found = 0;
/*
	int dev_id, dd;
	bdaddr_t bdaddr;
	
	if ((dev_id =  hci_get_route(NULL)) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_get_route() failed\n", __FUNCTION__);
		return;
	}
	
	if((dd = hci_open_dev(dev_id)) < 0) {	// open the local hci dev for sending request
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_open_dev() failed\n", __FUNCTION__);
		return;
	}
*/
	if((linkkey_file_stream = OpenFile("linkkeys", "r")) == NULL)
		return;

	while(fgets(linkkey_read_buff, sizeof(linkkey_read_buff), linkkey_file_stream)) {
		memset(read_bt_add, 0 , sizeof(read_bt_add));
		snprintf(read_bt_add, sizeof(read_bt_add), "%s", linkkey_read_buff);
		//AddrStringRmColumn(read_bt_add);
		if((paireddev_file_stream = OpenFile("paireddevice", "r")) == NULL)
			return;
		while(fgets(paireddev_read_buff, sizeof(paireddev_read_buff), paireddev_file_stream)) {
			//printf("finding %s in %s", read_bt_add, paireddev_read_buff);
			found = FindAddr(paireddev_read_buff, read_bt_add);
			if(found) {
				//printf("found = %d\n", found);
				snprintf(read_bt_name, sizeof(read_bt_name), "%s", paireddev_read_buff+strlen(read_bt_add)+1);
				break;
			}
			memset(paireddev_read_buff, 0, sizeof(paireddev_read_buff));
		}
		fclose(paireddev_file_stream);
/*		
		str2ba(read_bt_add, &bdaddr);
		memset(read_bt_name, 0, sizeof(read_bt_name));
		
		if(hci_read_remote_name(dd, &bdaddr, sizeof(read_bt_name), read_bt_name, 25000) < 0) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_read_remote_name() failed\n", __FUNCTION__);
			memset(read_bt_name, 0, sizeof(read_bt_name));
			snprintf(read_bt_name, sizeof(read_bt_name), "%s", "Unknown");
		}
*/		
		AddrStringRmColumn(read_bt_add);
		if(!found) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: friendly name is not found.\n", __FUNCTION__);
			snprintf(read_bt_name, sizeof(read_bt_name), "%s", read_bt_add);
		}
		if(*(read_bt_name+strlen(read_bt_name) - 1) == '\n')
			*(read_bt_name+strlen(read_bt_name) -1) = '\0';
		snprintf(resp_string, sizeof(resp_string), "%s,\"%s\"", read_bt_add, read_bt_name);
		
		printf("Trust Dev: %s\n", resp_string);
		SendResponse(sockfd, resp_string);
		memset(resp_string, 0, sizeof(resp_string));
		memset(linkkey_read_buff, 0, sizeof(linkkey_read_buff));
		found = 0;
	}

	fclose(linkkey_file_stream);
}

/*********************************************************************** 
* Description:
* search for the given device from the trusted device list
* 
* Calling Arguments: 
* Name			Description 
* paddr		the address of the device needs to be removed.
*
* Return Value: 
* 0:	error
* 1: 	success
*/
int SearchPairedDev(const char *addr) {
	FILE *file_stream;
	char read_buff[BUFF_SIZE] = {};
	long int file_size = 0;
	int found = 0;

	if(!ValidAddr(addr)) 
		return 0;
	
	if((file_stream = OpenFile("linkkeys", "r")) == NULL)
		return 0;

	fseek(file_stream, 0, SEEK_END);
	file_size = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET);

	if (file_size <= 0)
		return 0;

	while(fgets(read_buff, sizeof(read_buff), file_stream)) {
		found = FindAddr(read_buff, addr);
		if(found) 
			break;
		memset(read_buff, 0, sizeof(read_buff));
	}

	return found;
}

/*********************************************************************** 
* Description:
* remove the given trusted device from the trusted device list
* 
* Calling Arguments: 
* Name			Description 
* paddr		the address of the device needs to be removed.
*
* Return Value: 
* 0:	error
* 1: 	success
******************************************************************************/
int RmTrustDev(const char *addr) {
/*	FILE *file_stream;
	char read_buff[BUFF_SIZE] = {};
	char *pnewfile = NULL;
	//char addr[BT_ADDR_LENGTH] = {};
	long int file_size = 0;
	int found = 0;
*/
	//snprintf(addr, sizeof(addr), "%s", paddr);
	
//	AddrStringAddColumn(addr);
//	printf("%s: addr %s\n", __FUNCTION__, addr);
	if(!ValidAddr(addr)) {
		printf("Error: Invalid BT device address (%s)\n", addr);
		return 0;
	}
	BTFindandDel("linkkeys", addr, NULL);
	BTFindandDel("paireddevice", addr, NULL);
	return 1;
/*	
	if((file_stream = OpenFile("linkkeys", "r")) == NULL)
		return 1;

	fseek(file_stream, 0, SEEK_END);
	file_size = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET);

	if (file_size <= 0)
		return 1;

	pnewfile = (char *)malloc(file_size);
	if(!pnewfile) {
		printf("Error: %s(%d) malloc()\n", __FUNCTION__, __LINE__);
		return 1;
	}
	memset(pnewfile, 0, file_size);
	
	while(fgets(read_buff, sizeof(read_buff), file_stream)) {
		if(found)		// if the address is found already, just copy the reading
			snprintf(pnewfile+strlen(pnewfile), file_size-strlen(pnewfile), "%s", read_buff);
		else {
			found = FindAddr(read_buff, addr);
			if(!found) 	// if the cur reading is not the one looking for.
				snprintf(pnewfile+strlen(pnewfile), file_size-strlen(pnewfile), "%s", read_buff);
		}
		memset(read_buff, 0, sizeof(read_buff));
	}
	CloseFile(file_stream);

	if(found) {
		file_stream = OpenFile("linkkeys", "w");
		fwrite(pnewfile, sizeof(char), strlen(pnewfile), file_stream);
		CloseFile(file_stream);
	}

	free(pnewfile);
	pnewfile = NULL;
	return 1;
*/
}

/*********************************************************************** 
* Description:
* remove the trusted device list
* 
* Calling Arguments: 
* Name			Description 
* none
*
* Return Value: 
* none
******************************************************************************/
void RmAllTrustDev(void) {
	char fullpath[FILENAME_SIZE] = {};

	GetBTFilePath(fullpath, "linkkeys");
	if(access(fullpath, F_OK) < 0)
		printf("%s not existed\n", fullpath);
	else 
		unlink(fullpath);

	GetBTFilePath(fullpath, "paireddevice");
	if(access(fullpath, F_OK) < 0)
		printf("%s not existed\n", fullpath);
	else 
		unlink(fullpath);
}

static void BTFindandDel(const char *filename, const char *addr, const char *new_string) {
	BTFindandReplace(filename, addr, NULL);
}

static int BTFindandReplace(const char *filename, const char *addr, const char *new_string) {
	FILE *file_stream;
	char read_buff[BUFF_SIZE] = {};
	char *pnewfile = NULL;
	long int file_size = 0;
	int found = 0;
	int res = 0;
	char bt_addr[BT_ADDR_LENGTH] = {};

	
	if((file_stream = OpenFile(filename, "r"))== NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: OpenFile() %s failed\n", __FUNCTION__, filename);
		goto end;
	}

	fseek(file_stream, 0, SEEK_END);
	file_size = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET);

	if (file_size <= 0) {
		res = 1;
		goto end;
	}
	

	pnewfile = (char *)malloc(file_size);
	if(!pnewfile) {
		printf("Error: %s(%d) malloc()\n", __FUNCTION__, __LINE__);
		return 1;
	}
	memset(pnewfile, 0, file_size);
	
	sprintf(bt_addr, "%s", addr);
	while(fgets(read_buff, sizeof(read_buff), file_stream)) {
	//	printf("read_buff: %s\n", read_buff);
		if(found)		// if the address is found already, just copy the reading
			snprintf(pnewfile+strlen(pnewfile), file_size-strlen(pnewfile), "%s", read_buff);
		else {
			if(strlen(bt_addr) == 12)
				AddrStringAddColumn(bt_addr);
			
			found = FindAddr(read_buff, addr);
			if(!found) 	// if the cur reading is not the one looking for.
				snprintf(pnewfile+strlen(pnewfile), file_size-strlen(pnewfile), "%s", read_buff);
		}
		memset(read_buff, 0, sizeof(read_buff));
	}
	CloseFile(file_stream);

	if(found) {
		file_stream = OpenFile(filename, "w");
		fwrite(pnewfile, sizeof(char), strlen(pnewfile), file_stream);
		CloseFile(file_stream);
	}

	free(pnewfile);
	pnewfile = NULL;
	
end:

	if(new_string)  {// append to the end of the file
		file_stream = OpenFile(filename, "a");
		fwrite(new_string, sizeof(char), strlen(new_string), file_stream);
		CloseFile(file_stream);
	}
	return res;
}

// 0: success
// -1: fail
static int GetFriendlyNameFromInq(char *name, const char *bt_addr) {
	char name_string[BT_NAME_LENGTH] = {};
	FILE *file_stream = NULL;
	int file_size = 0;
	char read_buff[BUFF_SIZE] = {};
	char bt_addr_full[BT_ADDR_LENGTH] = {};
	int found = 0;
	
	
	if((file_stream = OpenFile("names", "r"))== NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: OpenFile() names failed\n", __FUNCTION__);
		goto end;
	}

	fseek(file_stream, 0, SEEK_END);
	file_size = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET);

	if (file_size <= 0) {
		CloseFile(file_stream);
		goto end;
	}

	snprintf(bt_addr_full, sizeof(bt_addr_full), "%s", bt_addr);
	AddrStringAddColumn(bt_addr_full);
	while(fgets(read_buff, sizeof(read_buff), file_stream)) {
		if(found = FindAddr(read_buff, bt_addr_full))
			break;

		memset(read_buff, 0, sizeof(read_buff));
	}

	CloseFile(file_stream);
	
end:	
	if(found) {
		snprintf(name, BT_NAME_LENGTH*sizeof(char), "%s", read_buff+strlen(bt_addr_full)+1);
		return 0;
	} else
		return -1;
}

char *GetPairingDeviceName(const char *addr) {
//	FILE *file = NULL;
	int dev_id, dd;
//	char read_buff[BUFF_SIZE] = {};
	char bt_addr[BT_ADDR_LENGTH] = {};
	char read_bt_name[BT_NAME_LENGTH] = {};
	bdaddr_t bdaddr;
	char *pstring = NULL;
	int string_leng = 0;
	
	// get friendly name
	if ((dev_id =  hci_get_route(NULL)) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_get_route() failed\n", __FUNCTION__);
		return NULL;
	}
	
	if((dd = hci_open_dev(dev_id)) < 0) {	// open the local hci dev for sending request
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_open_dev() failed\n", __FUNCTION__);
		return NULL;
	}

	sprintf(bt_addr, "%s", addr);
	str2ba(addr, &bdaddr);
	//printf("|%s|\n", addr);
	AddrStringRmColumn(bt_addr);
	
	if(hci_read_remote_name(dd, &bdaddr, sizeof(read_bt_name), read_bt_name, 25000) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: hci_read_remote_name() failed\n", __FUNCTION__);
		memset(read_bt_name, 0, sizeof(read_bt_name));
		if(GetFriendlyNameFromInq(read_bt_name, bt_addr) < 0) {	// try to get friendly name from the AT+BTIN result. if it's still failed then using *+btaddress
			memset(read_bt_name, 0, sizeof(read_bt_name));
			snprintf(read_bt_name, sizeof(read_bt_name), "*%s", bt_addr);
		}
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: set Friendly Name of  %s to be *%s\n", __FUNCTION__, bt_addr, read_bt_name);
	}

	//printf("%s's friendly name: %s\n", addr, read_bt_name);
	string_leng = strlen(addr) + strlen(read_bt_name) + 3; // format of "bt_addr,friendlyname\n\0"
	pstring = (char *)malloc(string_leng * sizeof(char));
	if(pstring == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: malloc() failed\n", __FUNCTION__);
		return NULL;
	}
	memset(pstring, 0, string_leng * sizeof(char));

	snprintf(pstring, string_leng * sizeof(char), "%s,%s\n", addr, read_bt_name);
	return pstring;
}

void UpdatePairedDevice(const char *addr, const char *pstring) {
	BTFindandReplace("paireddevice", addr, pstring);
	return;
}