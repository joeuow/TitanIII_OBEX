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
* File Name:			wl_bluetooth_tool.c
* Last Modified:
* Changes:
**********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include "debuglog.h"
#include "wl_bluetooth_tool.h"
#include "hci_info.h"
#include "ositech_obex.h"
#include "sdp_op.h"
#include "rfcomm_op.h"
#include "ositech_bt.h"


#define DEBUG_MSG(fmt, ...) do {\
	char msg[DEBUGMSG_LENG] = {}; \
	snprintf(msg, sizeof(msg), fmt, __VA_ARGS__); \
	if(DEBUGLOG) debuglog(LOG_INFO, "[BT_tool] %s(%d): %s", __FUNCTION__, __LINE__, msg);\
	if(DEBUG) printf("%s(%d): %s", __FUNCTION__, __LINE__, msg);	\
} while(0);
	
struct BTdev_info dev_list;

static struct help_instruct helps[] = {
	{"-c", "[BT addr]", "Pairing with the BT"},	
	{"-f", "[BT addr]", "Start OBEX session to the BT"},
	{"-d", "[BT addr]", "Start Service on the remote BT device"},
	{"-i", 0, "Inquiry remote BT device(s)"},
	{"-n", "[Friendly Name]", "Set the friendly name of the local BT adaptor"},
	{"-p", "[Pin code]", "Set Pin code for pairing"},
	{"-r", "[BT addr]", "Remove the pairing with the BT"},	
	{"-s", "[BT addr]", "Start SERIAL connection to the BT"},
	{"-h", 0, "Help"},
	{NULL, NULL, NULL}
};

static uint num_local_service = 4;
struct local_service support[] = {
		{"1000", 'd'},	// sdp
		{"1101", 's'}, // serial port
		{"1105", 'f'},	// obex object push
		{"1106", 'f'}, // obex file transfer
//		{NULL, NULL}
};
/*
static void *LoadingProcess(void *arg) {
	int *done = (int *)arg;
	int state = 0;
	
	while(*done == 0) {
		switch(state % 4) {
			case 0: printf("|\b");
			break;
			case 1: printf("/\b");
			break;
			case 2: printf("-\b");
			break;
			case 3: printf("\\\b");
			break;
		}
		fflush(stdout);
		state++;
		usleep(50000);
	}
	printf(" \n");
	pthread_exit(NULL);
}
*/
static void Usage(void) {
	int i;

	for(i=0; helps[i].option; i++) {
		printf("\t%-4s%4s\t%s\n", 
			helps[i].option, 
			helps[i].argument? helps[i].argument:" ",
			helps[i].desc);
	}

}
// -1: fail
//   0: success
/*********************************************************************** 
* Description:
* Create the csv file to store the BT inquiry result
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		success or not
******************************************************************************/
static int CreateCsv(void) {
	FILE *pfile; 

	if(!access(BT_PEERS_PROFILES_CSV_FILE, F_OK))
		unlink(BT_PEERS_PROFILES_CSV_FILE);

	if((pfile = fopen(BT_PEERS_PROFILES_CSV_FILE, "a+")) == NULL) {
		DEBUG_MSG("fopen() failed -- %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void SaveInCsv(const char *string) {
	FILE *pfile; 
	int wr_sz;

	if((pfile = fopen(BT_PEERS_PROFILES_CSV_FILE, "a+")) == NULL) {
		perror("fopen csv file failed");
		return;
	}

	if (!strcmp(string, "\n"))
		wr_sz = fprintf(pfile, "%s", string);
	else {
		wr_sz = fprintf(pfile, "%s;", string);
		if(strlen(string) != wr_sz-strlen(";")) 
			printf("WARNING: saving %s to csv file may be incompleted %d - %d.\n", string, strlen(string), wr_sz);
	}
	
	fclose(pfile);
}

static void Uint2String(unsigned int value, char *p) {
	char tmp[16] = {};
	unsigned int offset = 0;
	unsigned int index = 0;
	int i;
	
	if (value <= 0) {
		*p = '0';
		return;
	}

	while(1) {
		if (value/10)	{
			tmp[index++] = value % 10 + '0';
			value /= 10 ;
		} else {
			tmp[index] = value + '0';
			break;
		}
	}
	for(i = index; i >=0; i--) {
		*(p+offset++) = tmp[i];
	}
}

static void ListAppend(struct BTdev_info *header, struct BTdev_info *pdev) {
	struct BTdev_info *ptemp = header->pnext;

	if(ptemp == NULL)
		header->pnext = pdev;
	else {
		while(ptemp->pnext) ptemp = ptemp->pnext;
		ptemp->pnext = pdev;
	}	
}

static void DisplayList(struct BTdev_info header) {
	struct BTdev_info *ptemp = header.pnext;

	if(ptemp == NULL) return;

	while(ptemp) {
		printf("%-16s: %s\n", ptemp->dev_name, ptemp->dev_addr);
		ptemp = ptemp->pnext;
	}
}

static void DelPin(void) {
	unlink(WL_PINCODE_FILE);
}

/*********************************************************************** 
* Description:
* Valid the BT address .
* 
* Calling Arguments: 
* Name			Description 
* char		given address of the BT device 
*
* Return Value: 
* int		result
******************************************************************************/
static int ValidBTAddr(const char *addr) {
	int i = 0;
	
	if(strlen(addr) != 17) {
		DEBUG_MSG("Invalid given address length %d (Right length: 17)\n", strlen(addr));
		return 0;
	}

	while(i < strlen(addr)) {
		if ((*(addr + i) <= '9' && *(addr + i) >= '0') ||
			( *(addr + i) >= 'A' && *(addr + i) <= 'F') ||
			( *(addr + i) >= 'a' && *(addr + i) <= 'f') ||
			( *(addr + i) >= ':'))
			i++;
		else {
			DEBUG_MSG("%c is invalid\n", *(addr + i));
			DEBUG_MSG("Invalid given address %s\n", addr);
			return 0;
		}
	}

	return 1;
}

/*********************************************************************** 
* Description:
* Valid the BT given pin code .
* 
* Calling Arguments: 
* Name			Description 
* char		given pin code of the BT device 
*
* Return Value: 
* int		result
******************************************************************************/
static int ValidBTPin(const char *code) {
	int code_leng = strlen(code);

	if (code_leng < 0 || code_leng > 8) // max length is 8
		return 0;
	else 
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
* 0:	fail
* 1: 	success
******************************************************************************/
static int MatchBTAddr(char *reading, const char *addr) {
//	char string[READ_BUFF_LENG] = {};
	const char dlim[] = ": ";
	char *ptok;
	int addr_index = 0;
	int addr_len = strlen(addr);

//	snprintf(string, BUFF_SIZE*sizeof(char), "%s", reading);
	ptok = strtok(reading, dlim);
	if(!strncmp(ptok, addr, strlen(ptok))) { // compare first 2 value of the address
		addr_index += strlen(ptok)+1;	// strlen("xx:")
		while((ptok = strtok(NULL, dlim))) {
			if(!strncmp(ptok, addr+addr_index, strlen(ptok))) {
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

// 0 fail
// 1 success
/*********************************************************************** 
* Description:
* Check if the Titan contains the linkkey with the given BT address .
* 
* Calling Arguments: 
* Name			Description 
* char		given address of the BT device 
*
* Return Value: 
* int		result
******************************************************************************/
static int CheckBTLinkKey(const char *bt_addr) {
	FILE *file_stream;
	char read_buff[READ_BUFF_LENG] = {};
	long int file_size = 0;
	int found = 0;

	if((file_stream = OpenFile("linkkeys", "r")) == NULL)
		return 0;

	fseek(file_stream, 0, SEEK_END);
	file_size = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET);

	if (file_size <= 0) 
		goto end;
	
	while(fgets(read_buff, sizeof(read_buff), file_stream)) {
		if((found=MatchBTAddr(read_buff, bt_addr)))
			break;
		memset(read_buff, 0, sizeof(read_buff));
	}
end:
	CloseFile(file_stream);
	return found;
}

static void GetBTServiceClassUUID(void *value, void *userdata)
{
	uuid_t *uuid = (uuid_t *)value;

	sdp_uuid2strn(uuid, userdata, MAX_LEN_UUID_STR);
}

// char: support
// 0: not support locally
static char FindLocalService(const char *uuid_string, struct local_service loc_sup_uuid[]) {
	int index;

	for(index = 0; index < num_local_service; index++) {
		if(!strcmp(uuid_string, loc_sup_uuid[index].UUID))
			return loc_sup_uuid[index].option;
	}

	return '0';
}

// 0: success;
// -1: fail
static int OBEXOption(obexftp_client_t *cli, const char *pcmd, const char *parg) {
	int res = -1;

	DEBUG_MSG("%s %s\n", pcmd, parg?parg:"");
	if(!strcmp(pcmd, "CD") || !strcmp(pcmd, "cd")) {
		if(ChangeDir(cli, parg) == 1)
			res = 0;
	} else if(!strcmp(pcmd, "MD") || !strcmp(pcmd, "md")) {
		if(MakeDir(cli, parg) == 1)
			res = 0;
	} else if(!strcmp(pcmd, "PUT") || !strcmp(pcmd, "put")) {
		if(FTPTransFile(cli, parg, FTPFROMFILE, 0) > 0)
			res = 0;
	}else if(!strcmp(pcmd, "DIR") || !strcmp(pcmd, "dir")) {
		CreateDirXML();
		if(ListDir(cli) == 1)
			res = 0;
		GetDirXML(-1, DISPLAY_DIR_XML);
	} 

	return res; 
}

// -1: fail
// 0: success
static int RfcommSendString(const int sr_fd, const char *string) {
	int str_length;
	int wr_sum, wr_sz;
	//	wr_pos;

	if(string == NULL)
		return -1;
	str_length = strlen(string);

	wr_sum = 0;
	while(wr_sum < str_length) {	// keep writing till all sent or error
		//if((wr_sz = write(sr_fd, string+wr_pos, str_length-wr_pos)) < 0) {
		if((wr_sz = write(sr_fd, string+wr_sum, str_length-wr_sum)) < 0) {
			DEBUG_MSG("write to rfcomm0 failed (%s)\n", strerror(errno));
			return -1;
		}
		wr_sum += wr_sz;
//		wr_pos = str_length - wr_sum;
	}

	return 0;
}

// -1: fail
// 0: success
static int RfcommSendFile(const int sr_fd, const char *file) {
	char read_buff[READ_BUFF_LENG] = {};
	int fd, file_size, cur_pos;
	int rd_sz, wr_sz; //wr_pos;
	int rd_sum, wr_sum;
	int res = -1;

	if((fd = open(file, O_RDONLY)) < 0) {
		DEBUG_MSG("open %s failed (%s)\n", file, strerror(errno));
		return -1;
	}
	cur_pos = lseek(fd, 0, SEEK_CUR);
	file_size = lseek(fd, 0, SEEK_END);
	lseek(fd, cur_pos, SEEK_SET);

	rd_sz = 0;
	rd_sum = 0;
	while(rd_sum < file_size) {
		memset(read_buff, 0, sizeof(read_buff));
		if((rd_sz = read(fd, read_buff, sizeof(read_buff))) < 0) {
			DEBUG_MSG("read %s failed (%s)\n", file, strerror(errno));
			goto end;
		}
		rd_sum += rd_sz;
		if(rd_sum == file_size && *(read_buff+rd_sz-1) == 0xa)
			*(read_buff+rd_sz-1) = '\0';
		
		wr_sz = 0;
//		wr_pos = 0;
		wr_sum = 0;
		while(wr_sum < rd_sz) {	// keep writing till all sent or error
		//	if((wr_sz = write(sr_fd, read_buff+wr_pos, rd_sz-wr_pos)) < 0) {
			if((wr_sz = write(sr_fd, read_buff+wr_sum, rd_sz-wr_sum)) < 0) {
				DEBUG_MSG("write to rfcomm0 failed (%s)\n", strerror(errno));
				goto end;
			}
		
			wr_sum += wr_sz;
//			wr_pos = rd_sz - wr_sum;
		}
//		rd_sum += rd_sz;
	}
	
	res = 0;
end:
	close(fd);
	return res;
}
// 0: success;
// -1: fail
static int RfcommOption(const int sr_fd, const char *option) {
	char *parg = NULL;
	int res = 0;
	
	if(option) {	
		parg = strchr(option, ' ');
		parg += 1;
		if(!strncmp(option, "file", strlen("file"))) {
			if(RfcommSendFile(sr_fd, parg) < 0)
				res = -1;
		} else if(!strncmp(option, "string", strlen("string"))) 
			if(RfcommSendString(sr_fd, parg) < 0)
				res = -1;
	} 

	return res;
}

// 0: success
// -1: fail
static int RfcommRecvResp(const int sr_fd) {
	char *pbuff;
	char resp[READ_BUFF_LENG*10] = {};
	int rd_sz, ret; 
	fd_set rd_set;
	struct timeval timeout;
	int fd, file_size;
	char *pread_file;
	
	FD_ZERO(&rd_set);
	FD_SET(sr_fd, &rd_set);
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	if((pbuff = (char *)malloc(READ_BUFF_LENG*sizeof(char))) == NULL) {
		DEBUG_MSG("malloc() failed (%s)\n", strerror(errno));
		return -1;
	}

	do {
		memset(pbuff, 0, READ_BUFF_LENG*sizeof(char));
		if((ret = select(FD_SETSIZE, &rd_set, NULL, NULL, &timeout)) < 0) {
			DEBUG_MSG("select() Failed %s\n", strerror(errno));
			return -1;
		} else if (ret == 0) {
			break;
		} else {
			rd_sz = read(sr_fd, pbuff, READ_BUFF_LENG*sizeof(char));
			if(rd_sz <= 0) 
				break;
			else {		
				if(*(pbuff+rd_sz-1) == '\r' )
					*(pbuff+rd_sz-1) = '\n';
				snprintf(resp+strlen(resp), sizeof(resp)-strlen(resp), "%s", pbuff);
			}
		}
	} while (1);

	free(pbuff);

	if((fd = open(BT_TOOL_TEST_FILE, O_RDONLY)) < 0) {
		DEBUG_MSG("open() failed: %s\n", strerror(errno));
		return -1;
	}
	file_size= lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if((pread_file = (char *)malloc(file_size*sizeof(char))) < 0) {
		DEBUG_MSG("malloc() failed: %s\n", strerror(errno));
		return -1;
	}
	memset(pread_file, 0, file_size*sizeof(char));
	rd_sz=read(fd, pread_file, file_size*sizeof(char));
	if(rd_sz == file_size && *(pread_file+rd_sz-1) == 0xa)
			*(pread_file+rd_sz-1) = '\0';
	 
//	if(strlen(resp)) {
	//	if(*(resp + strlen(resp) - 1) == '\n') *(resp + strlen(resp) - 1) ='\0';
//		printf("Receive Response: %s\n", resp);
//	}

	if(strstr(resp, pread_file))
		return 0;
	else
		return -1;
}

static void CreateTestFile(void) {
	char cmd[256] = {};
	bdaddr_t addr;
	char addr_string[32];

	GetBTDevAdd(&addr);
	ba2str(&addr, addr_string);
	snprintf(cmd, sizeof(cmd), "echo \"Hello this is a Bluetooth test from the device %s!\" > %s",  addr_string, BT_TOOL_TEST_FILE);
	system(cmd);
}

static char *UseDefaultTestFile(void) {
	char *serial_option;
	
	if(access(BT_TOOL_TEST_FILE, F_OK) < 0) 
		CreateTestFile();
	
	if((serial_option = (char *)malloc(CMD_LENG*sizeof(char))) == NULL) {
		DEBUG_MSG("malloc() failed (%s)\n", strerror(errno));
		return NULL;
	}
	memset(serial_option, 0, CMD_LENG*sizeof(char));
	snprintf(serial_option, CMD_LENG*sizeof(char), "%s %s", "file", BT_TOOL_TEST_FILE);

	return serial_option;
}

// char: support
// 0: not support locally
static char MatchLocalBTSerivce(const sdp_record_t *rec, sdp_list_t **seqp) {
	char res = '0';
	char UUID_str[MAX_LEN_UUID_STR];
	
	if(sdp_get_uuidseq_attr(rec, SDP_ATTR_SVCLASS_ID_LIST, seqp) == 0){
		sdp_list_foreach(*seqp, GetBTServiceClassUUID, UUID_str);
		res = FindLocalService(UUID_str, support);
	}

	return res;
}

// 0 fail
// 1 success
/*********************************************************************** 
* Description:
* Set the friendly name of the local BT adaptor
* 
* Calling Arguments: 
* Name			Description 
* char		given name of the BT device 
*
* Return Value: 
* int		result
******************************************************************************/
static int BTTOOL_SetFriendlyName(char *name) {
	if(BTSetName(name)) {
		if(strlen(name))
			StoreName(name);
		else 
			DelNameFile();

		return 1;
	} else
		return 0;
}

/*********************************************************************** 
* Description:
* Set the friendly name of the local BT adaptor
* 
* Calling Arguments: 
* Name			Description 
* char		given name of the BT device 
*
* Return Value: 
* int		result
******************************************************************************/
static int BTTOOL_SavePin(const char *pin_code) {
	int fd;
	int wr_sz;
	unsigned int retry = 0;
	int pin_code_leng = strlen(pin_code);
	
	if(!pin_code)
		return 0;

	DEBUG_MSG("PIN code: %s\n", pin_code);
	if(!ValidBTPin(pin_code))
		DEBUG_MSG("%s is Invalid\n", pin_code);
	if((fd = open(WL_PINCODE_FILE, O_WRONLY | O_CREAT)) < 0) {
		DEBUG_MSG("open() failed -- %s\n", strerror(errno));
		return 0;
	}

	do {
		DEBUG_MSG("%s\n", "Saving the PIN code.");
		if((wr_sz = write(fd, pin_code, pin_code_leng)) < 0) {
			DEBUG_MSG("write() failed -- %s\n", strerror(errno));
			return 0;
		}

		if (retry++ >= 3)
			break;
	} while(wr_sz < pin_code_leng);	//wr_sz < pin_code length, rewrite

	return 1;
}

// -1: fail
//   0: success
static int BTToolStart_Inq(void) {
	int dev_id, dd;
	int num_rsp;
	int res = 0;
	char addr[ADDR_LENG];
	char dev_name[DEVNAME_LENG];
	inquiry_info *info = NULL;
	int i;
	struct BTdev_info *bt_peer;

	dev_id = hci_get_route(NULL);
	DEBUG_MSG("hci_get_route() dev_id = %d\n", dev_id);
	if (dev_id < 0) {
		res = -1;
		goto end;
	}
	
	dd = hci_open_dev(dev_id);	// open the local hci dev for sending request
	DEBUG_MSG( "hci_open_dev() returns dd = %d\n", dd);
	if (dd < 0) {
		res = -1;
		goto end;
	}

	num_rsp = hci_inquiry(dev_id, 8, 0, NULL, &info, IREQ_CACHE_FLUSH);
	DEBUG_MSG( "hci_inquiry() returns num_rsp = %d\n", num_rsp);
	if (num_rsp < 0) {
		res = -1;
		goto end1;
	}

	for(i = 0; i < num_rsp; i++) {
		memset(addr, 0, sizeof(addr));
		memset(dev_name, 0, sizeof(dev_name));
		ba2str(&(info+i)->bdaddr, addr);
		
		if (hci_read_remote_name(dd, &(info+i)->bdaddr, sizeof(dev_name), dev_name, 25000) == 0) {
			bt_peer = (struct BTdev_info *)malloc(sizeof(*bt_peer));
			if(!bt_peer) {
				DEBUG_MSG("malloc() failed -- %s\n", strerror(errno));
				res = -1;
				break;
			}
			memset(bt_peer, 0, sizeof(struct BTdev_info));
			snprintf(bt_peer->dev_name, sizeof(bt_peer->dev_name), "%s", dev_name);
			snprintf(bt_peer->dev_addr, sizeof(bt_peer->dev_addr), "%s", addr);
			bt_peer->profile = 0;
			bt_peer->pnext = NULL;
			ListAppend(&dev_list, bt_peer);
		}
	}
	
	bt_free(info);
end1:
	hci_close_dev(dev_id);
end:
	DEBUG_MSG("returns %s\n", !res?"success":"fail");
	return res;
}

// 0: success
// -1: fail
static int BTToolStart_SDP(const char *bt_addr) {
	sdp_list_t *next, *pcount;
	sdp_list_t *seq = NULL;
	uint8_t profile_count = 0;
	char profile_count_string[4] = {};
	char option;
	int res;

	if(!CheckBTLinkKey(bt_addr)) {
		DEBUG_MSG("%s is NOT paried\n", bt_addr);
		return -1;
	}
	
	if(CreateCsv())
		return -1;
	SaveInCsv(bt_addr);

	res = BrowseBTServices(bt_addr, &seq);
	DEBUG_MSG("BrowseBTServices() %s\n", (res>0)?"success":"failed");	
	if (res < 0) {
		Uint2String(profile_count, profile_count_string);
		SaveInCsv(profile_count_string);
		return -1;
	}
		
	// count number of enabled profiles.
	for(pcount = seq; pcount; pcount = pcount->next) {
		sdp_record_t *rec = (sdp_record_t *) pcount->data;
		sdp_data_t *d = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
		if (d && strlen(d->val.str)) {
			profile_count++;
		}
	}
			
	Uint2String(profile_count, profile_count_string);
	SaveInCsv(profile_count_string);
	DEBUG_MSG("found %d Service\n", profile_count);
		
	for (; seq; seq = next) {
		sdp_record_t *rec = (sdp_record_t *) seq->data;
		sdp_data_t *d = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
		sdp_list_t *list = 0;
		if (d && strlen(d->val.str)) {
			if(!strncmp(d->val.str, "COM", strlen("COM")))	// replace name of service "COMXX" with "Serial Port"
				SaveInCsv("Serial Port");
			else
				SaveInCsv(d->val.str);
			option = MatchLocalBTSerivce(rec, &list);
			sdp_list_free(list, free);
			SaveInCsv(&option);
		}
		next = seq->next;
		free(seq);
		sdp_record_free(rec);
	}
		
	SaveInCsv("\n");
	res = 0;
	
	DEBUG_MSG("returns %s\n", !res?"success":"fail");
	return res;
}

// -1: fail
//   0: success
static int BTToolStart_Pair(const char *bt_addr) {
	bdaddr_t bdaddr;
	int dd, dev_id, res;
	uint16_t handle;
	unsigned int ptype = HCI_DM1 | HCI_DM3 | HCI_DM5 | HCI_DH1 | HCI_DH3 | HCI_DH5;
	
	str2ba(bt_addr, &bdaddr);

	dev_id = hci_get_route(&bdaddr);	// ensure the connecting BT dev is not the local and then return the dev_id of local.
	DEBUG_MSG("hci_get_route() dev_id = (%d)\n", dev_id);
	if (dev_id < 0) {
		res = -1;
		goto end;
	}
	
	dd = hci_open_dev(dev_id);	// open the local hci dev for sending request
	DEBUG_MSG( "hci_open_dev() returns dd = %d\n", dd);
	if (dd < 0) {
		res = -1;
		goto end;
	}
	
	if (hci_create_connection(dd, &bdaddr, htobs(ptype), htobs(0x0000), 0x01, &handle, 25000) < 0) {
		DEBUG_MSG("hci_create_connection() failed -- %s\n", strerror(errno));
		res = -1;
		goto end1;
	}

	res = 0;
	
end1:	
	hci_close_dev(dd);
	DEBUG_MSG("returns %s\n", !res?"success":"fail");
end:
	return res;
}

// -1: fail
// 	0: success
static int BTToolStart_OBEX(const char *bt_addr, char *obex_cmd) {
	int res = -1;
	int channel = -1; 
	unsigned char *client = NULL;
	obexftp_client_t *cli = NULL;
	const char deli[] = " ";
	char *pcmd = NULL;
	char *parg = NULL;

	if(!CheckBTLinkKey(bt_addr)) {
		DEBUG_MSG("%s is NOT paried\n", bt_addr);
		return -1;
	}
	
	if(SearchBTwithObex(bt_addr, &channel) < 0) {
		DEBUG_MSG("NO OBEX service found on %s\n", bt_addr);
		goto end;
	} else
		DEBUG_MSG("OBEX service found on %s channel %d\n", bt_addr, channel);

	if(EstablisBTConnection(bt_addr, channel, &client) < 0) {
		DEBUG_MSG("CANNOT establish OBEX connection with %s\n", bt_addr);
		goto end;
	}
	cli = (obexftp_client_t *)client;

	// FTP cmds parsing
	DEBUG_MSG("OBEX cmd: %s\n", obex_cmd);
//	if(obex_cmd == NULL) {
		// put test.txt
//	} else {
	pcmd = strtok(obex_cmd, deli);
	if(pcmd) {
		parg = strtok(NULL, deli);
		if((res = OBEXOption(cli, pcmd, parg)))
			goto end1;
	}
	/* if there are any more cmd */
	while((pcmd = strtok(NULL, deli))) {
		if(pcmd) {
			parg = strtok(NULL, deli);
			if((res = OBEXOption(cli, pcmd, parg)))
				goto end1;
		}
	}
//	}

end1:	
	if(cli) {
		ReleasBTConnection(cli);
		cli = NULL;
	}
end:	
	DEBUG_MSG("returns %s\n", !res?"success":"fail");
	return res;
}

// -1: fail
// 	0: success
static int BTToolStart_Serial(const char *bt_addr, char *serial_option, int testbed) {
	int res = -1;
	int channel = 0;
	int fd;

	if(serial_option == NULL)
		goto end;
		
	if(!CheckBTLinkKey(bt_addr)) {
		DEBUG_MSG("%s is NOT paried\n", bt_addr);
		goto end;
	}

	if((res = SearchBTwithSerial(bt_addr, &channel)) < 0)
		goto end;
		
	if((fd = RfcommConnect(bt_addr, channel)) < 0)
		goto end;

	RfcommOption(fd, serial_option);
	usleep(10000);
	if(testbed)
		res = RfcommRecvResp(fd);
	else
		res = 0;
	
	RfcommDisconnect(fd);
	
end:
	DEBUG_MSG("returns %s\n", (res == 0)?"success":"fail");
	return res;
}

int main(int argc, char *argv[]) {
	int opt;
	char bt_addr[ADDR_LENG];
	char pin_code[PINCODE_LENG];
	char bt_friendly_name[BT_NAME_LENGTH];

	uint8_t init_pair = 0;
	uint8_t bt_inq = 0;
	uint8_t service_inq = 0;
	uint8_t start_obex = 0;
	uint8_t start_serial = 0;
	uint8_t testbed = 0;
	uint8_t rm_dev = 0;
	int led_org;
	
	memset(bt_addr, 0, sizeof(bt_addr));
	memset(pin_code, 0, sizeof(pin_code));
	memset(bt_friendly_name, 0, sizeof(bt_friendly_name));

	memset(&dev_list, 0, sizeof(dev_list));
	
	if(argc < 2) {
		Usage();
		exit(1);
	}
	if(DEBUGLOG) debuglog_enable = 1;
	
	while((opt = getopt(argc, argv, "c:d:f:hip:s:n:r:"))  != -1) {
		switch(opt) {
			case 'c':
				strcpy(bt_addr, optarg);
				DEBUG_MSG("Pairng with BT address %s\n", bt_addr)
				init_pair = 1;
				break;
			case 'd':
				service_inq = 1;
				strcpy(bt_addr, optarg);
				DEBUG_MSG("Service discover on BT address %s\n", bt_addr);
				break;
			case 'f':
				strcpy(bt_addr, optarg);
				DEBUG_MSG("OBEX connection with BT address %s\n", bt_addr);
				start_obex = 1;
				break;
			case 'i':
				DEBUG_MSG("%s\n", "Inquiry BT peers in the range.");
				bt_inq = 1;
				break;
			case 'n':
				strcpy(bt_friendly_name, optarg);
				DEBUG_MSG("Set Friendly name %s\n", bt_friendly_name);
				if(!BTTOOL_SetFriendlyName(bt_friendly_name))
					exit(1);
				break;
			case 'p':		
				strcpy(pin_code, optarg);
				DEBUG_MSG("Set PIN code %s\n", pin_code);
				if(!BTTOOL_SavePin(pin_code))
					exit(1);
				break;
			case 'r':
				strcpy(bt_addr, optarg);
				DEBUG_MSG("Remove BT device %s\n", bt_addr);
				rm_dev = 1;
				break;
			case 's':
				strcpy(bt_addr, optarg);
				DEBUG_MSG("Serial connection with BT address %s\n", bt_addr);
				start_serial= 1;
				break;
			case 'h':
			default:
				Usage();
				exit(1);
		}
	}

	// Once the BT_tool is started, unless the DEBUG is enabled, the printing out message would be
	// [BT_tool]: Start [Pairing/Inquiring/Service Discovery/OBEX Connection/Serial Connection]
	// [BT_tool]: Done [Pairing/Inquiring/Service Discovery/OBEX Connection/Serial Connection] --[Success/Fail]
	// validate the given BT address

	if(bt_inq) {
		printf("[BT_tool] Start Inquiring\n");
		led_org = GetCurBTLed();
		SetBTLed(BT_LED_FLASH_INQ);
		
		if(BTToolStart_Inq())
			printf("[BT_tool] Done Inquiring -- Fail\n");
		else {
			printf("[BT_tool] Done Inquiring -- Success\n");
			DisplayList(dev_list);
		}
		
		SetBTLed(led_org);
		goto done;
	}

	if(init_pair) {
		printf("[BT_tool] Start Pairing with %s.\n", bt_addr);
		led_org = GetCurBTLed();
		SetBTLed(BT_LED_SOLID);

		// validata if BT address is paired
		if(!SearchPairedDev(bt_addr)) {
			// validate if PIN code is set
			if(!strlen(pin_code)) {
				DEBUG_MSG("%s", "PIN code is not given, using Default PIN code: 1234.\n");
				BTTOOL_SavePin("1234");
			}
			if(!ValidBTAddr(bt_addr)) {
				printf("[BT_tool] Done Pairing -- Fail\n");
			} else {
				if(!BTToolStart_Pair(bt_addr))
					printf("[BT_tool] Done Pairing -- Success\n");
				else
					printf("[BT_tool] Done Pairing -- Fail\n");
			}
		} else {
			DEBUG_MSG("%s is ALREADY paired before. For re-pairing, remove it first.\n", bt_addr);
			printf("[BT_tool] Done Pairing -- Success\n");
		}
		
		SetBTLed(led_org);
		goto done;
	}

	if (service_inq) {
		printf("[BT_tool] Start Service Discovry with %s.\n", bt_addr);
		led_org = GetCurBTLed();
		SetBTLed(BT_LED_FLASH_INQ);
		
		if(!ValidBTAddr(bt_addr)) {
			printf("[BT_tool] Done Pairing -- Fail\n");
		} else {
			if(!BTToolStart_SDP(bt_addr))
				printf("[BT_tool] Done Service Discovery -- Success\n");
			else
				printf("[BT_tool] Done Service Discovery -- Fail\n");
		}

		SetBTLed(led_org);
		goto done;
	}

	if(start_obex) {
		char obex_cmd[CMD_LENG] = {};
		int index;
		
		printf("[BT_tool] Start OBEX Connection with %s.\n", bt_addr);
		led_org = GetCurBTLed();
		SetBTLed(BT_LED_SOLID);
			
		if(!ValidBTAddr(bt_addr)) {
			if(DEBUGLOG) debuglog(LOG_INFO, "[BT_tool] %s: BT address %s is invalid\n", __FUNCTION__, bt_addr);
			printf("[BT_tool] Done OBEX Connection -- Fail\n");
		} else {
			if(argv[optind]) {
				for(index = optind; index < argc; index++)
					snprintf(obex_cmd+strlen(obex_cmd), sizeof(obex_cmd) - strlen(obex_cmd), "%s ", argv[index]);
			} else {
				if(access(BT_TOOL_TEST_FILE, F_OK) < 0) {
					CreateTestFile();
				}
				snprintf(obex_cmd, sizeof(obex_cmd), "put %s", BT_TOOL_TEST_FILE);
			}
			if(!BTToolStart_OBEX(bt_addr, obex_cmd))
				printf("[BT_tool] Done OBEX Connection -- Success\n");
			else
				printf("[BT_tool] Done OBEX Connection -- Fail\n");
		}

		SetBTLed(led_org);
		goto done;
	}

	if(start_serial) {
		char *serial_option;
		int option_leng = 0, index;
		
		printf("[BT_tool] Start Serial Connection with %s.\n", bt_addr);
		led_org = GetCurBTLed();
		SetBTLed(BT_LED_SOLID);
		
		if(!ValidBTAddr(bt_addr)) {
			if(DEBUGLOG) debuglog(LOG_INFO, "[BT_tool] %s: BT address %s is invalid\n", __FUNCTION__, bt_addr);
			printf("[BT_tool] Done Serial Connection -- Fail\n");
		} else {
			if(argv[optind]) {
				if(strncmp(argv[optind], "file", strlen("file")) 
					&& strncmp(argv[optind], "string", strlen("string"))
					&& strncmp(argv[optind], "loopback", strlen("loopback")))
				{
					DEBUG_MSG("Unkonw option %s\n", argv[optind]);
					printf("[BT_tool] Done Serial Connection -- Fail\n");
					goto done;
				} 
				if(!strncmp(argv[optind], "file", strlen("file")) || !strncmp(argv[optind], "string", strlen("string"))) {
					for(index = optind; index < argc; index++)
						option_leng += (strlen(argv[index])+argc);
					if((serial_option = (char *)malloc(option_leng*sizeof(char))) == NULL) {
						DEBUG_MSG("malloc() failed (%s)\n", strerror(errno));
						printf("[BT_tool] Done Serial Connection -- Fail\n");
						goto done;
					}
					memset(serial_option, 0, option_leng*sizeof(char));
					snprintf(serial_option+strlen(serial_option), option_leng*sizeof(char)-strlen(serial_option), "%s %s", argv[optind], argv[optind+1]);
				} else if(!strncmp(argv[optind], "loopback", strlen("loopback"))) {
					serial_option = UseDefaultTestFile();
					testbed = 1;
				}
			} else {
				serial_option = UseDefaultTestFile();
			}
		
			if(!BTToolStart_Serial(bt_addr, serial_option, testbed))
				printf("[BT_tool] Done Serial Connection -- Success\n");
			else
				printf("[BT_tool] Done Serial Connection -- Fail\n");

			free(serial_option);
		}

		SetBTLed(led_org);
		goto done;
	}

	if(rm_dev) {
		printf("[BT_tool] Remove BT device %s.\n", bt_addr);
		
		if(RmTrustDev(bt_addr))
			printf("[BT_tool] Done Remove BT device -- Success\n");
		else
			printf("[BT_tool] Done Remove BT device  -- Fail\n");

		goto done;
	}
	
done:
	// delete set PIN code
	if(strlen(pin_code) || init_pair || service_inq || start_obex || start_serial)
		DelPin();
	
	return 0;
}
