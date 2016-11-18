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
static void TestDisplayATString(const char *string) {
	int str_leng = strlen(string);
	int i = 0;

	printf("AT String is:\n");
	while(i < str_leng)
		printf("%x ", string[i++]);
	printf("\n");
}
#else
static void TestDisplayATString(const char *string) {

}
#endif

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
int TestInitMrxSocket(void) {
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
static void TestAddCharatEnd(char *string, const char ch) {
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
static int TestSendMsg(const int sock_fd, char *msg, unsigned int print) {
	int wr_sz = 0;

	if (!msg) {
		printf("%s(%d) msg is NULL\n", __FUNCTION__, __LINE__);
		return 0;
	}
	
	if (print) {
		printf("Send: %s --> ", msg);
		fflush(stdout);
	}
	
	TestAddCharatEnd(msg, CHAR_CR);
	TestDisplayATString(msg);

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
static int TestStrapATResp(char *dest_buff, const char *src_buff, const unsigned int buff_leng ) {
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
static int TestRecvMsg(const int sock_fd, char *rec_buff, const int rec_buff_leng) {
	int rd_sz;

	if((rd_sz = recv(sock_fd, rec_buff, rec_buff_leng, 0)) <0 ) {
		printf("%s (%d): recv Error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
		return (FAILURE);
	}
	
	return rd_sz;
}

static int TestReadSocket(const int sock_fd, char *resp_string, const int print) {
	int recv_sz = -1;
	char recv_buffer[RECV_BUFF_SIZE];
	
	memset(recv_buffer, 0, sizeof(recv_buffer));
	if ((recv_sz = TestRecvMsg(sock_fd, recv_buffer, sizeof(recv_buffer))) < 0) {
		return recv_sz;
	} else if (recv_sz) {
		TestDisplayATString((char *)recv_buffer);
		if(!TestStrapATResp(resp_string, recv_buffer, recv_sz)) {
			printf("Incorrect AT command format\n");
			return 0;
		}
		
		if(print) printf("Response: %s\n", resp_string);
	} 

	return strlen(resp_string);
}

// FTP QUIT
// 1: success
// 0: failed.
static int TestSendQUIT(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	int recv_sz = 0;
	int ftp_quit = 0;


//	printf("%s\n", __FUNCTION__);
	
	if((TestSendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		return 0;
	}
	while ((recv_sz = TestReadSocket(client_sockfd, resp_string, 1)) > 0) {	// recv  response
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
static int TestSendDIR(const int client_sockfd, char *cmd_string) {
	char resp_string[RECV_BUFF_SIZE] = {};
	char complete_resp_string[RECV_BUFF_SIZE*3] = {};
	int recv_sz = 0; 
	char *pend_dir = NULL;
	int ret = 0;
	
	if((TestSendMsg(client_sockfd, cmd_string, 1)) < 0) {
		printf("%s(%d): SendMsg Failed\n", __FUNCTION__, __LINE__);
		goto end;
	}
	memset(complete_resp_string, 0, sizeof(complete_resp_string));
	
	while ((recv_sz = TestReadSocket(client_sockfd, resp_string, 1)) > 0) {	// recv  response
		snprintf(complete_resp_string+strlen(complete_resp_string), 
			sizeof(complete_resp_string)-strlen(complete_resp_string),
			"%s", resp_string);

		/* 
			if the request cannot be handled properly by the BT peer,
			a "406 FTP" used to indicate the ERROR will be returned right way 
			and it should be handled.
		*/
		if(!strncmp(complete_resp_string, "406 FTP", strlen("406 FTP"))) {
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
				break;
			}
		
		memset(complete_resp_string, 0, sizeof(complete_resp_string));
		snprintf(complete_resp_string, sizeof(complete_resp_string), "%s", resp_string);
		memset(resp_string, 0, sizeof(resp_string));
	} 
	if (recv_sz < 0)
		printf("ReadSocket Failed\n");
	
end:
	return ret;	
}

static void TestStartMrxObex(const int client_sockfd, const int titan_mtu, uint8_t times) {
	char cmd_string[128] = {};
	
	printf("FTP CMD: ");
	snprintf(cmd_string, sizeof(cmd_string), "%s", "DIR-RAW");
	if(TestSendDIR(client_sockfd, cmd_string))
		printf("DIR-RAW Finished.\n");
	else
		printf("DIR-RAW Failed.\n");
	printf("--------\n");

	sleep(30*times);
	
	memset(cmd_string, 0, sizeof(cmd_string));
	snprintf(cmd_string, sizeof(cmd_string), "%s", "QUIT");
	TestSendQUIT(client_sockfd, cmd_string);
	printf("Exit from StartMrxObex\n");
}

int main() {
	int client_sockfd;
	int send_sz = 0;
	int recv_sz = 0;
	int count = 0;
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
	
	client_sockfd = TestInitMrxSocket();
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
	
	while(count++ < 100) {
		memset(cmd_string, 0, mtu*sizeof(char));
		snprintf(cmd_string, mtu*sizeof(char), "%s", "ATD70F39543F836");
		send_sz = TestSendMsg(client_sockfd, cmd_string, 0);
		if (send_sz < 0) {
			printf("SendMsg Failed\n");
			sleep(1);
			continue;
		}

		// keep reading the socket till either "OK" , "ERROR", "PAIR", "NO CARRIER" or "CONNECTED" is received
	
		memset(resp_string, 0, sizeof(resp_string));
		if ((recv_sz = TestReadSocket(client_sockfd, resp_string, 1)) < 0) {
			printf("ReadSocket Failed\n");
			sleep(1);
			break;
		} else if (recv_sz) {
			if ((!strncmp(resp_string, "BTUP", strlen("BTUP")))) {
				printf("at %d times\n", count);
				memset(resp_string, 0, sizeof(resp_string));
				if ((recv_sz = TestReadSocket(client_sockfd, resp_string, 1)) < 0)	// recv rest msg
					printf("ReadSocket Failed\n");
				else if (recv_sz > 0)
					TestStartMrxObex(client_sockfd, mtu, count); 
			} else {
				printf("ATD Failed at %d times\n", count);
				break;
			}
		} 
		
		printf("=============\n");
		sleep(5);
	}
	
exit:
	printf("End of MRx simulation.\n");
	shutdown(client_sockfd, SHUT_RDWR);
	free(cmd_string);
	return 0;
}

