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
* File Name:			ositech_obex.h
* Last Modified:
* Changes:
**********************************************************************/

#ifndef __OSITECH_OBEX_H
#define __OSITECH_OBEX_H

#include <obexftp/obexftp.h>
#include <obexftp/client.h>

#define ERROR	-1
#define NO_CARRIER	-2

// BT FTP cmd
#define	BT_FTP_QUIT	0xB1
#define	BT_FTP_CD		0xB2
#define	BT_FTP_MD		0xB3
#define	BT_FTP_GET_MAX	0xB4
#define	BT_FTP_PUT	0xB5
#define 	BT_FTP_DIR	0xB6
#define	BT_FTP_ABORT	0xB7

// BT FTP response
#define BT_FTP_SERVICE_SUCCESS		200

#define BT_FTP_SERVICE_UNAUTHORIZED	401
#define BT_FTP_SERVICE_NOT_FOUND	404
#define BT_FTP_SERVICE_UNACCEPTABLE	406
#define BT_FTP_SERVICE_INTERNAL_SERVER_ERROR		500

// BT FTP error
#define BT_FTP_UNKNOW_CMD		0xFF

// BT FTP PUT method
#define FTPFROMSOCKET	0x1
#define FTPFROMFILE	0x2

// BT FTP DIR command: print out the dir result
#define DISPLAY_DIR_XML 1

extern int StartFTPSession(const int cli_sockfd, unsigned char *client, const uint inactive_timeout, const int led_org);
extern int EstablisBTConnection(const char *device, const int channel, unsigned char **client);
extern int SearchBTwithObex(const char *addr, int *res_channel);
extern void ReleasBTConnection(obexftp_client_t *cli);
extern int ChangeDir(obexftp_client_t *cli, const char *name);
extern int MakeDir(obexftp_client_t *cli, const char *name);
extern int ListDir(obexftp_client_t *cli);
extern void DelDirXML(void);
extern int CreateDirXML(void);
extern int GetDirXML(const int sockfd, const int display);
extern int FTPTransFile(obexftp_client_t *cli, const char *filename, const int method, const int sockfd);
//extern int SendResponse(const int sockfd, const char *resp_string);

#endif