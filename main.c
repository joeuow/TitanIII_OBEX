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
* File Name:			main.c
* Last Modified:
* Changes:
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <debuglog.h>

#include "ositech_communication.h"
#include "ositech_obex.h"
#include "ositech_bt.h"
#include "config.h"

#define FAILURE	-1
#define SUCCESS	1

// configuratio of server
#define BASE_IP "192.168.171.2"
#define USING_PORT		2004
#define ALLOW_CLIENT_NUM	1

#define AT_ARG_LENG	512
#define FTP_SUCC_SIZE	128

static char *BT_cmd_string[] = {
	NULL,
	"Disable Echo",
	"Hang Up - Drop Connection",
	"S-Register",
	"Remove Trusted Device",
	"Remove All Trusted Devices",
	"Set Friendly Name",
	"Inquire",
	"Set PIN",
	"List Trusted Devices",
	"Initiate Pairing",
	"Start FTP Session",
	"Load Friendly Name"
};

/*********************************************************************** 
* Description:
* Parsing the AT command issued by the user to the Titan unit accordingly.
* 
* Calling Arguments: 
* Name			Description 
* cli_sockfd		the open socket id
*
* Return Value: 
* None
******************************************************************************/
static void connection_handler(int cli_sockfd, const uint inactive_timeout) {
	int cmd;
	uint8_t ftp_start = 0;
	int chanel = -1;
	char ftp_succ[FTP_SUCC_SIZE] = {};
	char arg[AT_ARG_LENG] = {};
	int led_org = 0;
	
	while((cmd = RecvCmd(cli_sockfd, arg, 0)) > 0) {
#ifdef DEBUG
		if (strlen(arg))
			printf("Arg: %s\n", arg);
#endif
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Command Arg - %s\n", __FUNCTION__, arg);

		if(cmd != BT_CMD_UNKNOWN) {
			printf("Command: %s\n", BT_cmd_string[cmd]);
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Command - %s\n", __FUNCTION__, BT_cmd_string[cmd]);
		} else {
			printf("Command: Unknown\n");
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Command - Unknown\n", __FUNCTION__);
		}
			
		switch (cmd) {
			case BT_NO_ECHO:
				SendResponse(cli_sockfd, "OK");
				break;
			case BT_HANG:
				SendResponse(cli_sockfd, "BTDOWN");
				break;
			case BT_REGISTERS:
				if(ValidRegisters(arg)) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: ValidRegisters() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: ValidRegisters() is failed because the given Argument is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 02");
				}
				break;
			case BT_REMOVE_PAIRED_DEV: {
				char addr[BT_ADDR_LENGTH] = {};
				snprintf(addr, sizeof(addr), "%s", arg);
				AddrStringAddColumn(addr);
				if(RmTrustDev(addr)) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: RmTrustDev() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: RmTrustDev() is failed because the given Address is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 01");
				}
				break;
			}
			case BT_REMOVE_PAIRED_DEVS:
				RmAllTrustDev();
				debuglog(LOG_INFO, "[titan_obex] %s: RmAllTrustDev() is done successfully.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "OK");
				break;
			case BT_SET_NAME:
				if(StrapQuote(arg) < 0) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BT_SET_NAME is failed because the given Argument is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 02");
					break;
				}
				if (ValidName(arg) == 0) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BT_SET_NAME is failed because the given Argument is beyond the length.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 03");
					break;
				}
				
				if(BTSetName(arg)) {
					if(strlen(arg))
						StoreName(arg);
					else 
						DelNameFile();
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTSetName() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTSetName() is failed because the BT hardware is not found.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 05");
				}
				break;
			case BT_LOAD_NAME: 
				{
					char *pname = NULL;
					int ret = BTLoadName(&pname);
					if (ret > 0) {
						SendResponse(cli_sockfd, pname);
						free(pname);
						pname = NULL;
					}
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTLoadName() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				}
				break;
			case BT_INQUIRE:
				led_org = GetCurBTLed();
				SetBTLed(BT_LED_FLASH_INQ);
				if(BTGetInq(cli_sockfd)) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTGetInq() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTGetInq() is failed because the BT hardware is not found.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 05");
				}
				SetBTLed(led_org);
				break;
			case BT_SET_PIN:
				if(StrapQuote(arg) < 0) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BT_SET_PIN is failed because the given Argument is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 02");
					break;
				}
				if (ValidPin(arg) == 0) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BT_SET_PIN is failed because the given Argument is beyond the length.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 03");
					break;
				}
				
				if(BTSetPIN(arg)) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTSetPIN() is done successfully.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "OK");
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTSetPIN is failed and no PIN code is set.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 04");
				}
				break;
			case BT_LIST_PAIRED_DEVS:
				GetTrustList(cli_sockfd);
				if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: GetTrustList() is done successfully.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "OK");
				break;
			case BT_INIT_PAIR:
			{
				int init = 0, res = 0;
				char resp[128] = {};
				char addr[BT_ADDR_LENGTH] = {};
				char *pstring = NULL;

				snprintf(addr, sizeof(addr), "%s", arg);
				AddrStringAddColumn(addr);
				if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Pairing BT device address %s.\n", __FUNCTION__, addr);
			//	res = BTInitPair(arg, &init);
				if((pstring = GetPairingDeviceName(addr)) == NULL) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Get friendly name of %s failed\n", __FUNCTION__, addr);
					printf("WARNING: Get friendly name of %s Failed\n", addr);
				}
				
				led_org = GetCurBTLed();
				usleep(1000);
				
				SetBTLed(BT_LED_SOLID);
				res = BTInitPair(addr, &init);
				if (init) {
					SendResponse(cli_sockfd, "OK"); // start pairing
					if(debuglog_enable) {
						if(res == 0)  {
							debuglog(LOG_INFO, "[titan_obex] %s: BTInitPair() is done successfully.\n", __FUNCTION__);
							//sleep(20);
							if(pstring) UpdatePairedDevice(addr, pstring);
						} else 
							debuglog(LOG_INFO, "[titan_obex] %s: BTInitPair() is failed in PAIRing.\n", __FUNCTION__);
					}
					if (res > 2) res = 2;
					snprintf(resp, sizeof(resp), "PAIR %d %s%s", res, arg, (!res)?" 00" : "\0");		
					SendResponse(cli_sockfd, resp); 
				} else {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BTInitPair() is failed because the given Address is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 01"); // didn't start pairing
				}
				if(!access(PINCODE_FILE, F_OK))
					unlink(PINCODE_FILE); // delete PIN code file, no matter if the pairing is successed or not.

				SetBTLed(led_org);
				if(pstring) free(pstring);
				break;
			}
			case BT_START_FTP:
				ftp_start = 1;
				break;
			default:
				if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: The issued AT command is unknown.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "ERROR 00");
				break;
		}
		if (ftp_start) {
			unsigned char *client = NULL;
			char addr[BT_ADDR_LENGTH] = {};
			int res = 0;

			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Starting FTP session.\n", __FUNCTION__);
			if(ParseATDArg(arg, addr) < 0) {
				debuglog(LOG_INFO, "[titan_obex] %s: Start FTP session failed because the given Argument is invalid.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "ERROR 02");
				goto next;
			}
			
			AddrStringAddColumn(addr);
			if (!strlen(addr)) {
				ftp_start = 0;
				printf("Address is not given\n");
				if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: BT device is not given.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "ERROR 01");
				goto next;
			} 
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Searching OBEX service on %s.\n", __FUNCTION__, addr);
			
			led_org = GetCurBTLed();
			SetBTLed(BT_LED_SOLID);
			if((res = SearchBTwithObex(addr, &chanel)) < 0) {
				ftp_start = 0;
				printf("Search OBEX service on %s failed\n", arg);
				if (res == ERROR) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: given BT device is invalid.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "ERROR 01");
				} else if (res == NO_CARRIER) {
					if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: NO OBEX service found on the BT device.\n", __FUNCTION__);
					SendResponse(cli_sockfd, "BTDOWN");
				}
				goto next;
			}
//			printf("Found OBEX sevice on %s Channel %d", device, chanel);
//			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Found FTP sevice on %s Channel %d", __FUNCTION__, device, chanel);
//			if((EstablisBTConnection(device, chanel, &client))<0) {
			printf("Found OBEX sevice on %s Channel %d\n", addr, chanel);
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Found FTP sevice on %s Channel %d\n", __FUNCTION__, addr, chanel);
			if((EstablisBTConnection(addr, chanel, &client))<0) {
				ftp_start = 0;
				printf("Connect with %s failed\n", arg);
				if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Start FTP session Failed.\n", __FUNCTION__);
				SendResponse(cli_sockfd, "BTDOWN");
				goto next;
			}
			snprintf(ftp_succ, sizeof(ftp_succ), "BTUP %s", arg);
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Start FTP session is Done successfully.\n", __FUNCTION__);
			SendResponse(cli_sockfd, ftp_succ);
			system("add_obex_service");
			StartFTPSession(cli_sockfd, client, inactive_timeout, led_org);	// once the StartFTPSession returns, it indicates the FTP Quit.
			client = NULL;
			system("del_obex_service");
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s: Quit from FTP session successfully.\n", __FUNCTION__);
			SendResponse(cli_sockfd, "BTDOWN");
next:
			SetBTLed(led_org);
			ftp_start = 0;
		}

		if(strlen(arg))
			memset(arg, 0, sizeof(arg));
		printf("==========\n");
	}
	
	printf("Connection Done\n");
}

/*********************************************************************** 
* Description:
* Init the socket for the connection between the local PC and the Titan.
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		the socket id
******************************************************************************/
static int InitMrxListener(void) {
	int serv_sockfd;
	int error = 0;
	struct sockaddr_in serv_addr;

	if((serv_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		error = errno;
		printf("%s (%d): socket Error: %s\n", __FUNCTION__, __LINE__, strerror(error));
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s (%d): socket() -- %s\n", __FUNCTION__, __LINE__, strerror(error));
		return (FAILURE);
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(USING_PORT);
	inet_aton(BASE_IP, &serv_addr.sin_addr);
	
	if (bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		error = errno;
		printf("%s (%d): bind Error: %s\n", __FUNCTION__, __LINE__, strerror(error));
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s (%d): bind() -- %s\n", __FUNCTION__, __LINE__, strerror(error));
		return (FAILURE);
	}
	
	listen(serv_sockfd, ALLOW_CLIENT_NUM);
	printf("Ositech Obex Daemon is listening...\n");
	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Ositech Obex Daemon is listening...\n");
	
	return serv_sockfd;
}

/*********************************************************************** 
* Description:
* main function of the application titan_obex
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		the result
******************************************************************************/
int main(void) {
	int serv_sockfd, cli_sockfd;
	unsigned int clilen;
	struct sockaddr_in cli_addr;
	char *pname = NULL;
	int ret;
	int error;
	uint inactive_timeout = 0;
	char entry[128] = {};
	char *pvalue;
	FILE *config_fd = fopen(CONFIG_FILE, "r");
	
	if (config_fd == NULL) {
		perror("Open /mnt/flash/config/conf/bt_obex.conf failed. Using default.");
		inactive_timeout = DEFAULT_INACTIVE_TIMEOUT;
		debuglog_enable = DEFAULT_DEBUGLOG_ENABLE;
	} else {
		while(fgets(entry, sizeof(entry), config_fd) != 0) {
			if (!strncmp(entry, "inactive.timeout=", strlen("inactive.timeout="))) {
				pvalue = strchr(entry, '=');
				pvalue += 1;
				if (pvalue)
					inactive_timeout = atoi(pvalue);
				if (inactive_timeout <= 0) {
					printf("The setting of inactive.timeout is invalid. Set it as default value.\n");
					inactive_timeout = DEFAULT_INACTIVE_TIMEOUT;
				}
			} else if (!strncmp(entry, "debuglog.enable=", strlen("debuglog.enable="))) {
				pvalue = strchr(entry, '=');
				pvalue += 1;
				if (!strncmp(pvalue, "NO", strlen("NO")) ||!strncmp(pvalue, "no", strlen("no")))
					debuglog_enable = DEBUGLOG_OFF;
				else
					debuglog_enable = DEBUGLOG_ON;
			}
			
			memset(entry, 0, sizeof(entry));
		}
	}
	
	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] ** Start titan_obex now **\n");
	printf("Debuglog: %s, Inactive.timeout: %d\n", debuglog_enable? "Enable" : "Disable", inactive_timeout);
	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] ** debuglog: %s, inactive.timeout: %d **\n", debuglog_enable? "Enable" : "Disable", inactive_timeout);
	if((serv_sockfd = InitMrxListener()) < 0) {
		printf("Init the Mrx listener Failed\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s Init the Mrx listener Failed\n", __FUNCTION__);
		return -1;
	}

	ret = BTLoadName(&pname);
	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s BTLoadName() returns %d\n", __FUNCTION__, ret);
	if (ret > 0) {
		BTSetName(pname);
		free(pname);
		pname = NULL;
	} else if (ret < 0)
		return -1;
	
	while(1) {
		clilen = sizeof(cli_addr);
		if((cli_sockfd = accept(serv_sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0)  {
			error = errno;
			printf("%s (%d): accept Error: %s\n", __FUNCTION__, __LINE__, strerror(error));
			if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s (%d) accept() - %s\n", __FUNCTION__, __LINE__, strerror(error));
			return (FAILURE);
		}

		printf("A connection: %s:%d is connecting.\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] A connection: %s:%d is connecting.\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
		connection_handler(cli_sockfd, inactive_timeout);
//		shutdown(cli_sockfd, SHUT_RDWR);
	} 

	return 0;
}
