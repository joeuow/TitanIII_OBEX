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
* File Name:			ositech_communication.c
* Last Modified:
* Changes:
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <debuglog.h>

#include "ositech_communication.h"
#include "ositech_obex.h"
#include "config.h"

#define CHAR_CR	0x0D
#define CHAR_LF	0x0A
#define AT_PREFIX	"AT"

#define FTP_RESP_BUFF_SIZE	128
#define BT_ADDR_LENG	18

/*********************************************************************** 
* Description:
* display the receiving/sending AT string 
* 
* Calling Arguments: 
* Name			Description 
* string		the AT string needs to be displayed
*
* Return Value: 
* none
******************************************************************************/
#ifdef DEBUG
void DisplayATString(const char *string) {
	int str_leng = strlen(string);
	int i = 0;

	printf("AT String is:\n");
	while(i < str_leng)
		printf("%x ", string[i++]);
	printf("\n");
}
#else
void DisplayATString(const char *string) {
	return;
}
#endif


// ***** Send Response *****
/*********************************************************************** 
* Description:
* generate the sending resonse according to the designed communication protol
* 
* Calling Arguments: 
* Name			Description 
* org_cmd		the origional AT string
* send_cmd		the generated AT string that could be sent out
*
* Return Value: 
* none
******************************************************************************/
static void CreateBtRespsonse(const char *org_cmd, char *send_cmd) {
	send_cmd[0] = CHAR_CR;
	send_cmd[1] = CHAR_LF;
	snprintf(send_cmd+strlen(send_cmd), strlen(org_cmd)*sizeof(char) + 1, "%s", org_cmd);
	send_cmd[strlen(send_cmd)] = CHAR_CR;
	send_cmd[strlen(send_cmd)] = CHAR_LF;
}

/*********************************************************************** 
* Description:
* send the string in FTP format
* 
* Calling Arguments: 
* Name			Description 
* sockfd		the socket used to send out the msg
* code		the status code of the FTP operation.
*
* Return Value: 
* int		bytes or error of sending
******************************************************************************/
int SendFTPResponse(int sockfd, const int code) {
	char resp_string[FTP_RESP_BUFF_SIZE] = {};

	snprintf(resp_string, sizeof(resp_string), "%d FTP", code);
	return SendResponse(sockfd, resp_string);
}

/*********************************************************************** 
* Description:
* send the string in AT format
* 
* Calling Arguments: 
* Name			Description 
* sockfd		the socket used to send out the msg
* resp_string	the string to send back.
*
* Return Value: 
* int		bytes or error of sending
******************************************************************************/
int SendResponse(const int sockfd, const char *resp_string) {
	int wr_sz;
	int error;
	char send_resp[RESP_BUFF_SIZE] = {};

	CreateBtRespsonse(resp_string, send_resp);
	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s(%d) send_resp -- %s\n", __FUNCTION__, __LINE__, send_resp);
	wr_sz = write(sockfd, send_resp, strlen(send_resp));
	if (wr_sz < 0) {
		error = errno;
		printf("%s(%d) write: %s\n", __FUNCTION__, __LINE__, strerror(error));
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s(%d) write -- %s\n", __FUNCTION__, __LINE__, strerror(error));
	}
	return wr_sz;
}

/*********************************************************************** 
* Description:
* Remove ":" to the given address
* 
* Calling Arguments: 
* Name			Description 
* addr		the given address
*
* Return Value: 
* none
******************************************************************************/
void AddrStringRmColumn(char *addr) {
	int pos = 2;
	char ptmp[13] = {};
	
	if (!addr)
		return;

	snprintf(ptmp, 3, "%s", addr);
	do {
		if(*(addr+pos) == ':') pos++;
		snprintf(ptmp+strlen(ptmp), 3, "%s", addr+pos);
		pos += 2;
	} while(pos < strlen(addr));

	memset(addr, 0, strlen(addr));
	snprintf(addr, strlen(ptmp)+1, "%s\n", ptmp);
}

/*********************************************************************** 
* Description:
* Add ":" to the given address
* 
* Calling Arguments: 
* Name			Description 
* addr		the given address
*
* Return Value: 
* none
******************************************************************************/
void AddrStringAddColumn(char *addr) {
	int pos = 0;
	char ptmp[BT_ADDR_LENG] = {};
	
	if (!addr)
		return;
	
	do {
		snprintf(ptmp+strlen(ptmp), 3, "%s", addr+pos);
		pos += 2;
		if(pos < strlen(addr))
			snprintf(ptmp+strlen(ptmp), 2, "%s", ":");
	} while(pos < strlen(addr));

	memset(addr, 0, strlen(addr));
	snprintf(addr, strlen(ptmp)+1, "%s\n", ptmp);
}

//***** Get CMD *****/
/*********************************************************************** 
* Description:
* Get the real content of the AT command string.
* 
* Calling Arguments: 
* Name			Description 
* string		the string searched for
* substring	the string searching for
*
* Return Value: 
* the real contents of the string
******************************************************************************/
static const char *FindString(const char *string, const char *substring) {
	unsigned int substring_pos;
	unsigned int string_pos = 0;
	char substring_first = *substring;
	const char *pchar;
	
	if (string == NULL || substring == NULL)
		goto error;
	if (!strlen(string) || !strlen(substring))
		goto error;

	for (string_pos = 0; string_pos < strlen(string); string_pos++) {
		if((pchar = strchr(string+string_pos, substring_first))) {
			for(substring_pos = 1; substring_pos < strlen(substring); substring_pos++) {
				if (*(pchar+substring_pos) != *(substring+substring_pos)) 
					break;
				if (substring_pos == strlen(substring) - 1)
					return pchar;
			}
		} else
			break;
	}

error:
	return NULL;
}

/*********************************************************************** 
* Description:
* remove character from the tail of the string
* 
* Calling Arguments: 
* Name			Description 
* buff		the target string
* ch 		the target character
*
* Return Value: 
* none
******************************************************************************/
static void RmCharfromTail(char *buff, const char ch) {
	int leng = strlen(buff);

	if (buff[leng-1] == ch ) buff[leng-1] = '\0';
}

void String2Upper(char *string_tmp, const char *org_string) {
	int string_leng = strlen(org_string);
	int i;

	for(i=0; i < string_leng; i++) {
		if ((*(org_string+i) >= 'a') && (*(org_string+i) <= 'z')) 
			*(string_tmp+i) = *(org_string+i) - ('a' - 'A');
		else
			*(string_tmp+i) = *(org_string+i);
	}
}

/*********************************************************************** 
* Description:
* remove the double quotes of the string at the beginning and the end.
* 
* Calling Arguments: 
* Name			Description 
* arg		the target string
*
* Return Value: 
* -1: error
* 0 : success
******************************************************************************/
int StrapQuote(char *arg) {
	char *ptmp = NULL;
	int arg_leng = strlen(arg);

	if (arg_leng < 2) { //at least 2 for the double quote
		printf("Error: Argument is not specified or in correct format.\n");
		return -1;
	}
	
	if ((arg[0] != '"') || (arg[arg_leng-1] != '"')) {
		printf("Error: Argument is not specified or in correct format.\n");
		return -1;
	}
	
	if(!(ptmp = (char *)malloc(arg_leng))) {
		printf("Error: %s(%d) malloc()\n", __FUNCTION__, __LINE__);
		return -1;
	}
	memset(ptmp, 0, arg_leng);
	snprintf(ptmp, arg_leng-1, "%s", arg+1);		//strap 2x double quote.
	memset(arg, 0, arg_leng);
	snprintf(arg, strlen(ptmp)+1, "%s", ptmp);
	
	free(ptmp);
	return 0;
}

// return 0xff to indicate that the AT command is Unknown
// otherwise corresponding AT command is sent.
/*********************************************************************** 
* Description:
* Get general bluetooth AT command.
* 
* Calling Arguments: 
* Name			Description 
* ptmp		the command string
* arg		the argument string of the command
*
* Return Value: 
* 0xff: unknown command
* else: commands
******************************************************************************/
static int GetGeneralCMD(const char *pup_string, const char *porg_string, char *arg) {
	int cmd= BT_CMD_UNKNOWN;
	char atcmd[BT_CMD_BUFF_SIZE] = {};
	
	if (!strlen(pup_string))
		goto end;
	
	snprintf(atcmd, BT_CMD_BUFF_SIZE, "%s", pup_string+strlen(AT_PREFIX));
	if(!strlen(atcmd))
		goto end;

#ifdef DEBUG
	printf("AT CMD -- [%s]\n", atcmd);
#endif

	if(!strcmp(atcmd, "E0"))
		cmd = BT_NO_ECHO;
	else if (!strcmp(atcmd, "H"))
		cmd = BT_HANG;
	else if(!strncmp(atcmd, "S", strlen("S"))) {
		cmd = BT_REGISTERS;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("S")+strlen(AT_PREFIX));
	}
	else if (!strncmp(atcmd, "+BTD", strlen("+BTD")) && strncmp(atcmd, "+BTD*", strlen("+BTD*"))) {
		cmd = BT_REMOVE_PAIRED_DEV;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("+BTD")+strlen(AT_PREFIX));
	} 
	else if (!strcmp(atcmd, "+BTD*"))
		cmd = BT_REMOVE_PAIRED_DEVS;
	else if (!strncmp(atcmd, "+BTF=", strlen("+BTF="))) {
		cmd = BT_SET_NAME;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("+BTF=")+strlen(AT_PREFIX));
	} else if (!strcmp(atcmd, "+BTF?")) 
		cmd = BT_LOAD_NAME;
	else if (!strcmp(atcmd, "+BTIN")) 
		cmd = BT_INQUIRE;
	else if (!strncmp(atcmd, "+BTK=", strlen("+BTK="))) {
		cmd = BT_SET_PIN;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("+BTK=")+strlen(AT_PREFIX));
	} 
	else if ( !strcmp(atcmd, "+BTT?")) 
		cmd = BT_LIST_PAIRED_DEVS;
	else if (!strncmp(atcmd, "+BTW", strlen("+BTW"))) {
		cmd = BT_INIT_PAIR;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("+BTW")+strlen(AT_PREFIX));
	} else if (!strncmp(atcmd, "D", strlen("D"))) {
		cmd = BT_START_FTP;
		snprintf(arg, BT_CMD_BUFF_SIZE, "%s", porg_string+strlen("D")+strlen(AT_PREFIX));
	} 

//	printf("cmd %d\n", cmd);
end:	
	return cmd;
}

/*********************************************************************** 
* Description:
* Get  bluetooth FTP command.
* 
* Calling Arguments: 
* Name			Description 
* buff		the command string
* arg		the argument string of the command
*
* Return Value: 
* 0xff: unknown command
* else: commands
******************************************************************************/
//static int GetFTPCMD(const char *buff, char *arg) {	
int GetFTPCMD(const char *pup_string, const char *porg_string, char *arg) {
	if (!strcmp(pup_string, "QUIT"))
		return BT_FTP_QUIT;
	else if (!(strcmp(pup_string, "MAX")))
		return BT_FTP_GET_MAX;
	else if (!(strncmp(pup_string, "CD", strlen("CD")))) {
		snprintf(arg, strlen(porg_string), "%s", porg_string+strlen("CD "));
		if(!strcmp(arg, "\\"))
			return BT_FTP_CD;
		if(StrapQuote(arg) < 0) {
			return BT_FTP_UNKNOW_CMD;
		}
		return BT_FTP_CD;
	} else if (!(strncmp(pup_string, "MD", strlen("MD")))) {
		snprintf(arg, strlen(porg_string), "%s", porg_string+strlen("MD "));
		if(StrapQuote(arg) < 0) {
			return BT_FTP_UNKNOW_CMD;
		}
		return BT_FTP_MD;
	} else if (!(strncmp(pup_string, "PUT", strlen("PUT")))) {
		snprintf(arg, strlen(porg_string), "%s", porg_string+strlen("PUT "));
		if(StrapQuote(arg) < 0) {
			return BT_FTP_UNKNOW_CMD;
		}
		return BT_FTP_PUT;
	} else if (!(strncmp(pup_string, "DIR -RAW", strlen("DIR -RAW")))) {
		return BT_FTP_DIR;
	} else if (!(strncmp(pup_string, "ABORT", strlen("ABORT")))) {
		return BT_FTP_ABORT;
	}
	
	return BT_FTP_UNKNOW_CMD;
}

/*********************************************************************** 
* Description:
* receive the message on the socket
* 
* Calling Arguments: 
* Name			Description 
* sockfd		the id of the socket open for receiving
* buff		the buffer to store the receiving contents.
* buff_leng	the length of the buffer.
*
* Return Value: 
* -1:	error 
* 0: no data
* >0: beytes being received.
******************************************************************************/
int RecvSocketMsg(const int sockfd, char *buff, const int buff_leng) {
	int recv_sz;
	int error;
	
	recv_sz = recv(sockfd, buff, buff_leng, 0);
	if (recv_sz < 0 ) {
		error = errno;
		printf("%s (%d): read Error: %s\n", __FUNCTION__, __LINE__, strerror(error));
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] Error: %s (%d) read -- %s\n", __FUNCTION__, __LINE__, strerror(error));
		return -1;
	} else if (!recv_sz) {
		printf("No data received\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s (%d) read no Data\n", __FUNCTION__, __LINE__);
		return 0;
	} 

	if(debuglog_enable) debuglog(LOG_INFO, "[titan_obex] %s returns %d\n", __FUNCTION__, recv_sz);
	return recv_sz;
}

// 0xFF: the receiving string is unknown.
// -1: the read errno
// 0: no data received
// (0, 0xFF): correct
/*********************************************************************** 
* Description:
* receive the command on the socket from the MRx
* 
* Calling Arguments: 
* Name			Description 
* sockfd		the id of the socket open for receiving
* arg		the argument string corresponding to the command.
*
* Return Value: 
* 0xFF: the receiving string is unknown.
* -1: the read errno
* 0: no data received
* (0, 0xFF): correct
******************************************************************************/
int RecvCmd(const int sockfd, char *arg, const int ftp_start) {
	int ret;
	char buff[BUFFER_SIZE] = {};
	char string_tmp[BUFFER_SIZE] = {};
	
	memset(buff, 0, sizeof(buff));
	if((ret = RecvSocketMsg(sockfd, buff, sizeof(buff))) <= 0)
		return ret;
	
#ifdef DEBUG
	DisplayATString(buff);
#endif
	RmCharfromTail(buff, CHAR_CR);

	String2Upper(string_tmp, buff);
//	printf("New string is:\n");
	DisplayATString(string_tmp);
/*
	if(FindString(string_tmp, AT_PREFIX))
		return GetGeneralCMD(string_tmp, buff, arg);
	else
		return GetFTPCMD(string_tmp, buff, arg);
*/
	if(!ftp_start)
		return GetGeneralCMD(string_tmp, buff, arg);
	else
		return GetFTPCMD(string_tmp, buff, arg);
}
