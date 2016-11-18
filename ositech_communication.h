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
* File Name:			ositech_communication.h
* Last Modified:
* Changes:
**********************************************************************/

#ifndef __CMD_H
#define __CMD_H

// BT cmd buff size
#define BT_CMD_BUFF_SIZE		512

#define RESP_BUFF_SIZE	1024
#define BUFFER_SIZE		512

// BT cmd
#define	BT_NO_ECHO	0x1	
#define	BT_HANG	0x2
#define	BT_REGISTERS	0x3
#define	BT_REMOVE_PAIRED_DEV	0x4
#define	BT_REMOVE_PAIRED_DEVS	0x5
#define	BT_SET_NAME	0x6
#define	BT_INQUIRE		0x7
#define	BT_SET_PIN		0x8
#define	BT_LIST_PAIRED_DEVS	0x9
#define	BT_INIT_PAIR	0xA
#define	BT_START_FTP	0xB
#define	BT_LOAD_NAME	0xC

// BT cmd unknown
#define 	BT_CMD_UNKNOWN	0xFF

extern int SendFTPResponse(int sockfd, const int code);
extern int SendResponse(const int sockfd, const char *resp_string);
extern int RecvCmd(const int sockfd, char *arg, const int ftp_start);
extern int RecvSocketMsg(const int sockfd, char *buff, const int buff_leng);
extern int StrapQuote(char *arg);
extern void AddrStringRmColumn(char *addr);
extern void AddrStringAddColumn(char *addr);
void String2Upper(char *string_tmp, const char *org_string);
int GetFTPCMD(const char *pup_string, const char *porg_string, char *arg);


#endif
