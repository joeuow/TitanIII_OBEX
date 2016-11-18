/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source 
 * code package.
 */

/*********************************************************************** 
* Original Author: 		Joe Wei
* File Creation Date: 	May/22/2016
* Project: 			MRX OBES
* Description: 		
* File Name:			MRx_simulator.c
* Last Modified:
* Changes:
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#define BASE_IP "192.168.171.2"
#define USING_PORT		2004

#define RECV_BUFF_SIZE	1024
#define MTU_STRING_LENG	32

#define FAILURE	-1
#define SUCCESS	1

#define CHAR_CR	0x0D
#define CHAR_LF	0x0A

#define MRX_STREAM_CHUNK 4096

static void Convert2Upper(char *cmd_string);
static void *SendAbortCmd(void *arg);

typedef struct arg {
	int sockfd;
	int thread_exit;
} abort_arg;

/*********************************************************************** 
* Description:
* Print out the 
* 
* Calling Arguments: 
* Name			Description 
* cli_sockfd		the open socket id
*
* Return Value: 
* None
******************************************************************************/
#ifdef DEBUG
static void DisplayATString(const char *string) {
	int str_leng = strlen(string);
	int i = 0;

	printf("AT String is:\n");
	while(i < str_leng)
		printf("%x ", string[i++]);
	printf("\n");
}
#else
static void DisplayATString(const char *string) {

}
#endif

/*********************************************************************** 
* Description:
* Convert integer to string 
* 
* Calling Arguments: 
* Name			Description 
* value		contering integer
* p			store the converted integer string
*
* Return Value: 
* None
******************************************************************************/
static void MRxInt2String(int value, char *p) {
	char tmp[16] = {};
	unsigned int offset = 0;
	unsigned int index = 0;
	int i;
	
	if (!value) {
		*p = '0';
		return;
	}
	else if (value < 0) *(p+offset++) = '-';

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

/*********************************************************************** 
* Description:
* Convert string to integer 
* 
* Calling Arguments: 
* Name			Description 
* pstring			store the converted integer string
*
* Return Value: 
* int 		converted integer
******************************************************************************/
static int String2Int(const char *pstring) {
	int leng;
	int sign = 1;
	int val = 0;
	int index = 0;
	
	if (!pstring) {
		printf("%s(%d) string is NULL\n", __FUNCTION__, __LINE__);
		return 0;
	}

	if (*pstring == '-') {
		sign= -1;
		index = 1;
	}

	leng = strlen(pstring);
	while(index < leng) {
		if ((*(pstring+index) >= '0') && (*(pstring+index) <= '9')) {
			val = val*10 + (*(pstring+index) - '0');
		} else
			return 0;
		index++;
	}

	return (val*sign);
	
}

/*********************************************************************** 
* Description:
* Init a socket to communicate with the Titan 
* 
* Calling Arguments: 
* Name			Description 
*
* Return Value: 
* int 		socket id
* <0			error
******************************************************************************/
int InitMrxSocket(void) {
	int client_sockfd;
	struct sockaddr_in serv_addr;
	
	if((client_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("%s (%d): socket Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
		return (FAILURE);
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(USING_PORT);
	inet_aton(BASE_IP, &serv_addr.sin_addr);
	
	if (connect(client_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		printf("%s (%d): connect Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
		return (FAILURE);
	}

	return client_sockfd;
}

/*********************************************************************** 
* Description:
* Add the specified character at the tail of the string
* 
* Calling Arguments: 
* Name			Description
* string			destination string
* ch				specific character
*
* Return Value: 
* int 		socket id
* void
******************************************************************************/
static void AddCharatEnd(char *string, const char ch) {
	int string_leng = strlen(string);

	*(string + string_leng) = ch;
}

/*********************************************************************** 
* Description:
* Send msg to the Titan
* 
* Calling Arguments: 
* Name			Description
* sock_fd			socket id of the socket between the Titan and the MRx
* msg			sent out msg
* print			enable the msg display or not.
*
* Return Value: 
* <= 0		error
* >0			sent bytes
******************************************************************************/
static int SendMsg(const int sock_fd, char *msg, unsigned int print) {
	int wr_sz = 0;

	if (!msg) {
		printf("%s(%d) msg is NULL\n", __FUNCTION__, __LINE__);
		return 0;
	}
	
	if (print) {
		printf("Send: %s --> ", msg);
		fflush(stdout);
	}
	
	AddCharatEnd(msg, CHAR_CR);
	DisplayATString(msg);

	wr_sz = send(sock_fd, msg, strlen(msg), 0);
	if (wr_sz < 0) {
		printf("%s (%d): send Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
		return -1;
	}
#ifdef DEBUG
	printf("wr_sz %d\n", wr_sz);
#endif

	return wr_sz;
}

/*********************************************************************** 
* Description:
* Fetch the AT contents from the receiving response
* 
* Calling Arguments: 
* Name			Description
* dest_buff		buffer to store the AT contents
* src_buff		the receiving response
* buff_leng		length of the receiving response
*
* Return Value: 
* 1		success
* 0		error
******************************************************************************/
static int StrapATResp(char *dest_buff, const char *src_buff, const unsigned int buff_leng ) {
	int start_pos = 0, end_pos = 0;

	if (src_buff[0] == CHAR_CR && src_buff[1] ==CHAR_LF) 
		start_pos += 2;

	
	if (src_buff[buff_leng-2] == CHAR_CR && src_buff[buff_leng-1] == CHAR_LF)
		end_pos += 2;
	
	snprintf(dest_buff, buff_leng-end_pos-start_pos+1, "%s", src_buff+start_pos);

	return 1;
}

/*********************************************************************** 
* Description:
* receive the AT contents from the socket
* 
* Calling Arguments: 
* Name			Description
* sock_fd			socket id of the socket between the Titan and the MRx
* rec_buff		the buffer to store receiving response
* rec_buff_leng	length of the buffer
*
* Return Value: 
* < 0		success
*	int 		receiving response length
******************************************************************************/
static int RecvMsg(const int sock_fd, char *rec_buff, const int rec_buff_leng) {
	int rd_sz;

	if((rd_sz = recv(sock_fd, rec_buff, rec_buff_leng, 0)) <0 ) {
		printf("%s (%d): recv Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
		return (FAILURE);
	}
	
	return rd_sz;
}

static int ReadSocket(const int sock_fd, char *resp_string, const int print) {
	int recv_sz = -1;
	char recv_buffer[RECV_BUFF_SIZE];
	
	memset(recv_buffer, 0, sizeof(recv_buffer));
	if ((recv_sz = RecvMsg(sock_fd, recv_buffer, sizeof(recv_buffer))) < 0) {
		return recv_sz;
	} else if (recv_sz) {
		DisplayATString((char *)recv_buffer);
		if(!StrapATResp(resp_string, recv_buffer, recv_sz)) {
			printf("Incorrect AT command format\n");
			return 0;
		}
		
		if(print) printf("Response: %s\n", resp_string);
	} 

	return strlen(resp_string);
}

/*********************************************************************** 
* Description:
* Display the percentage of the sending file.
* 
* Calling Arguments: 
* Name			Description
* filesize			the size of the sending file
* sentsize		the size of sent
*
* Return Value: 
* void
******************************************************************************/
static void DisplayProgress(const int filesize, const int sentsize) {
	int percent = 0;

	percent = (float)sentsize / filesize * 100;
	printf("%d%%\b\b%c", percent, (percent<10) ? '\0':'\b');
	fflush(stdout);
}

/*********************************************************************** 
* Description:
* The followings are the command sent to the Titan to accomplish the bluetooth operation.
* 
* Calling Arguments: 
* Name			Description
* client_sockfd	the socket id of the socket
* ...
*
* Return Value: 
* int
******************************************************************************/
static int PutFile(const int client_sockfd, const char *filename, const int mtu, const int error_test) {
	int fd;
	char *buff = NULL;
	char resp[32] = {};
	char data__leng[MTU_STRING_LENG] = {}; // max is mtu which could be a 32 int at the max
	int rd_sz, wr_sz, recv_sz, total_wr;
	int diff = 0;
	int ret = 0;
	int file_size = 0;

	buff = (char *)malloc(mtu*sizeof(char));
	if (!buff) {
		printf("Error: %s(%d) malloc\n", __FUNCTION__, __LINE__);
		goto done;
	}
	memset(buff, 0, mtu*sizeof(char));

	fd = open(filename, O_RDONLY);
	if(fd < 0) {
		printf("open %s: %s\n", filename, strerror(errno));
		goto done1;
	}

	file_size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	
	printf("Sending file of size ...");
	fflush(stdout);
	total_wr = 0;
	while((rd_sz = read(fd, buff, mtu))) { 
		DisplayProgress(file_size, total_wr);
	//	printf("total_rd: %d\n", total_rd += rd_sz);

		// 1. confirm the size of sending data length 
		memset(data__leng, 0, sizeof(data__leng));
		MRxInt2String(rd_sz, data__leng);
		wr_sz = SendMsg(client_sockfd, data__leng, 0);
		
		memset(resp, 0, sizeof(resp));
		if((recv_sz = ReadSocket(client_sockfd, resp, 0)) > 0) {
			if(strcmp(resp, "?")) {		// not confirmed
				printf("%s(%d): Receiving response Error. Exit the FTP transmission.\n", __FUNCTION__, __LINE__);
				break;	
			}
		} else {		// no response received
			printf("%s(%d): Receiving response failed. Exit the FTP transmission.\n", __FUNCTION__, __LINE__);
			break;	
		}
		
		// 2. send the read data
		//printf("Sending data...");
		//fflush(stdout);
		wr_sz = send(client_sockfd, buff, rd_sz, 0);
		
		if (wr_sz < 0) {
			printf("%s (%d): send Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
			goto done;
		}
		else { // if sent size is less than the read size, keep send the rest till
			diff = rd_sz - wr_sz;
			while(diff) {
//				printf("diff %d\n", diff);
				wr_sz += send(client_sockfd, buff+wr_sz, diff, 0);
				diff = rd_sz - wr_sz;
			}
//			printf("%d bytes being sent->", wr_sz);
//			fflush(stdout);
		}
		memset(resp, 0, sizeof(resp));
		if((recv_sz = ReadSocket(client_sockfd, resp, 0)) > 0) {
			if(strcmp(resp, "!")) {		// not confirmed
				printf("%s(%d): Receiving response Error. Exit the FTP transmission.\n", __FUNCTION__, __LINE__);
				break;	
			}
		} else {	// no response received
			printf("%s(%d): Receiving response failed. Exit the FTP transmission.\n", __FUNCTION__, __LINE__);
			break;
		}

		total_wr += wr_sz;
//		printf("total_wr: %d\n", total_wr += wr_sz);
//		printf("-------\n");
		memset(buff, 0, mtu*sizeof(char));
	}

	close(fd);
	DisplayProgress(file_size, total_wr);
	printf("\n");
	
done1:
	// send 0 to socket to indicate the EOF of the file
	if(!error_test) {
		memset(data__leng, 0, sizeof(data__leng));
		MRxInt2String(0, data__leng);
		wr_sz = SendMsg(client_sockfd, data__leng, 1);	
	}
	
	free(buff);
	buff = NULL;
	
done:	
//	shutdown(client_sockfd, SHUT_WR);
	if(!error_test) {
		if ((rd_sz = ReadSocket(client_sockfd, resp, 1)) > 0) {
			if(!strncmp(resp, "200 FTP", strlen("200 FTP")))	{
				printf("PUT %s done\n", filename);
				ret = 1;
			}
		}	 	
		else if (rd_sz < 0)
			printf("ReadSocket Failed\n");
	}
	
	return ret;
//	exit(ret);
}

static void SendOther(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return ;
	}
	if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1))  < 0)
		printf("ReadSocket Failed\n");

	return;
}
// FTP MD
// 1: success
// 0: failed.
static int SendMD(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int ret = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {
		if(!strncmp(resp_string, "200 FTP", strlen("200 FTP")))	{
			ret = 1;
		}
	} 	
	else if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return ret;
}

// FTP MD
// 1: success
// 0: failed.
static int SendCD(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int ret = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {
		if(!strncmp(resp_string, "200 FTP", strlen("200 FTP")))	{
			ret = 1;
		}
	} 	
	else if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return ret;
}

// FTP QUIT
// 1: success
// 0: failed.
static int SendQUIT(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int ftp_quit = 0;


//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	while ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {	// recv  response
//		printf("%s: resp_string=%s\n", __FUNCTION__, resp_string);
		if(!strncmp(resp_string, "200 FTP", strlen("200 FTP")))	// 1st resp sentence
			ftp_quit = 1;
		else if(!strncmp(resp_string, "BTDOWN", strlen("BTDOWN"))) // 2nd resp sentence
			return ftp_quit;

		memset(resp_string, 0, sizeof(resp_string));
	} 
	
	if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return ftp_quit;	
}

// FTP DIR-RAW
// 1: success
// 0: failed.



static int SendDIR(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	char complete_resp_string[RECV_BUFF_SIZE*3] = {};
	int recv_sz = 0; 
	char *pend_dir = NULL;
	pthread_t child_thread_id;
	int ret = 0;
	abort_arg argument;
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		goto end;
	}
	memset(complete_resp_string, 0, sizeof(complete_resp_string));

	argument.sockfd = client_sockfd;
	argument.thread_exit = 0;
	if(pthread_create(&child_thread_id, NULL, SendAbortCmd, (void *)&argument) < 0) {
		perror("SendDIR(): pthread_create()");
		goto end;
	}
	
	while ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {	// recv  response
		snprintf(complete_resp_string+strlen(complete_resp_string), 
			sizeof(complete_resp_string)-strlen(complete_resp_string),
			"%s", resp_string);

		/* 
			if the request cannot be handled properly by the BT peer,
			a "406 FTP" used to indicate the ERROR will be returned right way 
			and it should be handled.
		*/
		if(!strncmp(complete_resp_string, "406 FTP", strlen("406 FTP"))) {
			argument.thread_exit = 1;
			 break;
		}
		/* 
			if the DIR-RAW request is handled properly by the BT peer, 
			the end of the response should be as follow:
				\r\n</folder-listing>\r\n
				\r\n200 FTP\r\n
		*/
		if ((pend_dir = strstr(complete_resp_string, "</folder-listing>"))) 	// found end of xml
			//printf("	!</folder-listing> FOUND !\n");
			pend_dir += strlen("</folder-listing>");	// found end of ftp cmd response
		else
			pend_dir = complete_resp_string;
		
		if ((pend_dir = strstr(pend_dir, "FTP"))) {
			pend_dir -= 4;		// back to the start of NNN FTP.
			if(!strncmp(pend_dir, "200 FTP", strlen("200 FTP")))	
				ret= 1;
				argument.thread_exit = 1;
				break;
			}
		
		memset(complete_resp_string, 0, sizeof(complete_resp_string));
		snprintf(complete_resp_string, sizeof(complete_resp_string), "%s", resp_string);
		memset(resp_string, 0, sizeof(resp_string));
	} 
	if (recv_sz < 0)
		printf("ReadSocket Failed\n");
	
	if(pthread_join(child_thread_id, NULL) < 0)
		perror("SendDIR(): pthread_join()");	
	
end:
	return ret;	
}
// FTP MAX
static int SendMAX(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int mtu = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	while ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {	// recv  response
		// a. if response indicates error: <cr,lf>404 ftp<cr,lf>
		// b. if response returns MTU: 
		// 1. 	<cr,lf>nnn<cr,lf> 
		//		<cr,lf>200 FTP<cr,lf>
		// 2. 	<cr,lf>nnn<cr,lf><cr,lf>200 FTP<cr,lf>
		
		// once "FTP" is found, it indicates the end of the repsonse.
		if (strstr(resp_string, "FTP")) {
			// if <cr> and <lf> are found, it's case b.2, parse it to get MTU
			if (strchr(resp_string, CHAR_CR) && strchr(resp_string, CHAR_LF)) {
				char tmp[MTU_STRING_LENG] = {};
				snprintf(tmp, strchr(resp_string, CHAR_CR)-resp_string+1, "%s", resp_string);
				mtu = String2Int(tmp);
			}
			return mtu;
		}
		else		// if FTP is not found, it's case b.1, parse it to get MTU.
			mtu = String2Int(resp_string);
		
		memset(resp_string, 0, sizeof(resp_string));
	} 

	if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return -1;	
}

// FTP ABOURT
static int SendAbort(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int ret = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {
		if(!strncmp(resp_string, "200 FTP", strlen("200 FTP")))	{
			ret = 1;
		}
	} 	
	else if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return ret;
}

static void *SendAbortCmd(void *para) {
	char cmd_string[64];
	char up_cmd[64];
	int cmd_leng;
	abort_arg *arg = (abort_arg *)para;
	int sockfd = arg->sockfd;
	fd_set read_set;
	struct timeval timeout;
	int res;
	
	printf("\nFTP CMD (ABORT only): ");
	fflush(stdout);
	while(1) {
		FD_ZERO(&read_set);
		FD_SET(STDIN_FILENO, &read_set);
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
	
		if((res = select(FD_SETSIZE, &read_set, NULL, NULL, &timeout))< 0) {
			perror("SendAbortCmd(): select()");
			break;
		} else if (res > 0) {
			if (FD_ISSET(STDIN_FILENO, &read_set)) {
				memset(cmd_string, 0, sizeof(cmd_string));
				read(STDIN_FILENO, cmd_string, sizeof(cmd_string));
		
				// get rid of '\n'
				if ((cmd_leng = strlen(cmd_string)) > 0) {
					cmd_leng--;
					if (cmd_string[cmd_leng] == '\n')  cmd_string[cmd_leng] = '\0';
					memset(up_cmd, 0, (cmd_leng+1)*sizeof(char));
					snprintf(up_cmd, (cmd_leng+1)*sizeof(char), "%s", cmd_string);
				}
 
				Convert2Upper(up_cmd);
//				printf("%s cmd: %s\n", __FUNCTION__, up_cmd);
				if(!strcmp(up_cmd, "ABORT")) {
					SendMsg(sockfd, up_cmd, 1);
					break;
				} else 
					printf("Error: only ABORT is accepted within the FTP DIR -RAW\n");
			}
		} else {
			if (arg->thread_exit)
				break;
		}
	}

	return 0;
}

// FTP QUIT
// 1: success
// 0: failed.
static int SendPUT(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;

//	printf("%s\n", __FUNCTION__);
	
	if((SendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) > 0) {
		if(strlen(resp_string) == strlen("!") && !strncmp(resp_string, "!", strlen("!")))	
			return 1;
		else 
			return 0;
	} 	
	else if (recv_sz < 0)
		printf("ReadSocket Failed\n");

	return 0;	
}

static void Convert2Upper(char *cmd_string) {
	int string_leng = strlen(cmd_string);
	int i;

	for(i=0; i < string_leng; i++) {
		if ((*(cmd_string+i) >= 'a') && (*(cmd_string+i) <= 'z')) 
			*(cmd_string+i) = *(cmd_string+i) - ('a' - 'A');
	}
}



static void StartMrxObex(const int client_sockfd, const int titan_mtu) {
	char *cmd_string = NULL;
	char *up_cmd = NULL;
	char filename[32] = {};
	int mtu = MRX_STREAM_CHUNK;
	int cmd_leng;
	int res;

	cmd_string = (char *)malloc(titan_mtu*sizeof(char));
	if(!cmd_string) {
		printf("Error: malloc()\n");
		return;
	}
	
	while(1) {
		printf("FTP CMD: ");
		fflush(stdout);

		memset(cmd_string, 0, titan_mtu*sizeof(char));
		fgets(cmd_string, titan_mtu*sizeof(char),  stdin);
		// get rid of '\n'
		if ((cmd_leng = strlen(cmd_string)) > 0) {
			cmd_leng--;
			if (cmd_string[cmd_leng] == '\n')  cmd_string[cmd_leng] = '\0';
			up_cmd = (char *)malloc((cmd_leng+1)*sizeof(char));
			if(!up_cmd) {
				printf("Error: malloc()\n");
				return;
			}
			memset(up_cmd, 0, (cmd_leng+1)*sizeof(char));
			snprintf(up_cmd, (cmd_leng+1)*sizeof(char), "%s", cmd_string);
		}

		// FTP MAX
		Convert2Upper(up_cmd);

		printf("%s\n", up_cmd);
		if(!strcmp(up_cmd, "MAX")) {
			mtu = SendMAX(client_sockfd, cmd_string);	// mtu should > 0
			printf("MTU = %d\n", mtu);
			if(mtu <= 0) {
				printf("MTU is less than 0. Using default value %d\n", MRX_STREAM_CHUNK);
				mtu = MRX_STREAM_CHUNK;
			}
		}
		// FTP PUT
		else if (!strncmp(up_cmd, "PUT ", strlen("PUT "))) {
			if(SendPUT(client_sockfd, cmd_string)) {		
				snprintf(filename, cmd_leng-strlen("PUT \"\"")+1, "%s", cmd_string+strlen("PUT \""));
				if(mtu <= 0)	mtu = MRX_STREAM_CHUNK;
				PutFile(client_sockfd, filename, mtu, 0);
			}
		}
		else if (!strncmp(up_cmd, "PUT-ERROR ", strlen("PUT-ERROR "))) {
			snprintf(filename, cmd_leng-strlen("PUT-ERROR \"\"")+1, "%s", cmd_string+strlen("PUT-ERROR \""));
				
			memset(cmd_string, 0, titan_mtu*sizeof(char));
			snprintf(cmd_string, titan_mtu*sizeof(char), "PUT \"%s\"", filename);
			printf("cmd_string: %s\n", cmd_string);
			
			if(SendPUT(client_sockfd, cmd_string)) {		
			//	snprintf(filename, cmd_leng-strlen("PUT \"\"")+1, "%s", cmd_string+strlen("PUT \""));
				if(mtu <= 0)	mtu = MRX_STREAM_CHUNK;
				PutFile(client_sockfd, filename, mtu, 1);
			}
		}
		// FTP MD
		else if (!strncmp(up_cmd, "MD ", strlen("MD "))) {
			if(SendMD(client_sockfd, cmd_string))
				printf("MD Success.\n");
			else
				printf("MD Failed.\n");
		}
		// FTP MD
		else if (!strncmp(up_cmd, "CD ", strlen("CD "))) {
			if(SendCD(client_sockfd, cmd_string))
				printf("CD Success.\n");
			else
				printf("CD Failed.\n");
		}
		//FTP QUIT
		else if(!strcmp(up_cmd, "QUIT")) {
			res = SendQUIT(client_sockfd, cmd_string);
			if (res) {
//				close(client_sockfd);
				break;
			}
		}
		// FTP DIR -RAW
		else if(!strcmp(up_cmd, "DIR -RAW")) {
			if(SendDIR(client_sockfd, cmd_string))
				printf("DIR -RAW Finished.\n");
			else
				printf("DIR -RAW Failed.\n");
		} 
		// FTP ABORT
		else if(!strcmp(up_cmd, "ABORT")) {
			if(SendAbort(client_sockfd, cmd_string))
				printf("ABORT Success.\n");
			else
				printf("ABORT Failed.\n");
		} else {
			SendOther(client_sockfd, cmd_string);
		}

		printf("--------\n");
	}

	free(up_cmd);
	up_cmd = NULL;
	printf("Exit from StartMrxObex\n");
}

int main() {
	int client_sockfd;
	int cmd_leng;
	int send_sz = 0;
	int recv_sz = 0;
	unsigned char keep_reading = 0;
	struct ifreq *ifr;
	struct ifreq ifr_if;
	struct ifconf conf;
	struct sockaddr_in *ifaddr = NULL;
	char data[4096] = {};
	int mtu = 0;
	
	char *cmd_string = NULL;
	char resp_string[RECV_BUFF_SIZE];

	printf("Version: %.1f\n", VERSION);
	printf("*************\n");
	
	client_sockfd = InitMrxSocket();
	if (client_sockfd <= 0)
		goto exit;

	memset(&conf, 0, sizeof(conf));
	conf.ifc_len = sizeof(data);
    	conf.ifc_buf = (caddr_t) data;
	ioctl(client_sockfd,SIOCGIFCONF,&conf);
	
	ifr = (struct ifreq*)data;
	while ((char*)ifr < data+conf.ifc_len) {
		memset(&ifr_if, 0, sizeof(ifr_if));
		strncpy(ifr_if.ifr_name, ifr->ifr_name, IFNAMSIZ);
		
//		ioctl(client_sockfd, SIOCGIFBRDADDR, &ifr_if);
		ioctl(client_sockfd, SIOCGIFADDR, &ifr_if);
		ifaddr = (struct sockaddr_in *)&ifr_if.ifr_addr;
		if (!strncmp(inet_ntoa(ifaddr->sin_addr), "192.168.171.", strlen("192.168.171."))) {
			ioctl(client_sockfd, SIOCGIFMTU, &ifr_if);
			printf("Communicate to TitanIII via Interface %s with MTU %d\n", ifr->ifr_name, ifr_if.ifr_mtu);
			mtu = ifr_if.ifr_mtu;
			break;
		}
		ifr = (struct ifreq*)((char*)ifr +sizeof(*ifr));
	}

	if (mtu) {
		cmd_string = (char *)malloc(mtu*sizeof(char));
		if(!cmd_string) {
			printf("Error: malloc()\n");
			goto exit;
		}
	} else
		goto exit;
	
	while(1) {
		printf("CMD: ");
		fflush(stdout);
		
		memset(cmd_string, 0, mtu*sizeof(char));
		fgets(cmd_string, mtu*sizeof(char),  stdin);
		// get rid of '\n'
		if ((cmd_leng = strlen(cmd_string)) > 0) {
			if (cmd_string[cmd_leng - 1] == '\n')  cmd_string[cmd_leng - 1] = '\0';
		}
				
//		printf("cmd_string length %d\n", strlen(cmd_string));
		if(!strncmp(cmd_string, "exit", strlen("exit")))
			goto exit;

		send_sz = SendMsg(client_sockfd, cmd_string, 1);
		if (send_sz < 0) {
			printf("SendMsg Failed\n");
			sleep(1);
			continue;
		}

		// keep reading the socket till either "OK" , "ERROR", "PAIR", "NO CARRIER" or "CONNECTED" is received
		do {
			memset(resp_string, 0, sizeof(resp_string));
			if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) < 0) {
				printf("ReadSocket Failed\n");
				sleep(1);
				break;
			} else if (recv_sz) {
				Convert2Upper(cmd_string);
				
				if ((!strncmp(cmd_string, "ATD", strlen("ATD"))) && 
					(!strncmp(resp_string, "BTUP", strlen("BTUP")))) {
						memset(resp_string, 0, sizeof(resp_string));
						if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) < 0)	// recv rest msg
							printf("ReadSocket Failed\n");
						else if (recv_sz > 0)
							StartMrxObex(client_sockfd, mtu);
						
					keep_reading = 0;
				} else if ((!strncmp(cmd_string, "AT+BTW", strlen("AT+BTW")))&& 
					(!strncmp(resp_string, "OK", strlen("OK")))) {
						memset(resp_string, 0, sizeof(resp_string));
						if ((recv_sz = ReadSocket(client_sockfd, resp_string, 1)) < 0)	// recv rest msg
							printf("ReadSocket Failed\n");
						
					keep_reading = 0; 
				} else {
					char *pCR = NULL;

					pCR = strrchr(resp_string, CHAR_CR); 
					if (!pCR)
						pCR = resp_string;
					else
						pCR += 1;
					
/*					if (strstr(pCR, "OK") ||
					strstr(pCR, "ERROR") ||
					strstr(pCR, "NO CARRIER") ||
					strstr(pCR, "PAIR") ||
					strstr(pCR, "CONNECTED"))
*/
					if (!strncmp(pCR, "OK", strlen("OK")) ||
					!strncmp(pCR, "ERROR", strlen("ERROR")) ||
					!strncmp(pCR, "BTDOWN", strlen("BTDOWN")))
						keep_reading = 0;
					else
						keep_reading = 1;
				}
			}
//			printf("keep reading? %d\n", keep_reading);
		} while (keep_reading);
		printf("=============\n");
	}
	
exit:
	printf("End of MRx simulation.\n");
	shutdown(client_sockfd, SHUT_RDWR);
	free(cmd_string);
	return 0;
}
