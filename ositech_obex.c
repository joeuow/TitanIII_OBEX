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
* File Name:			ositech_obex.c
* Last Modified:
* Changes:
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <debuglog.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <obexftp/obexftp.h>
#include <obexftp/client.h>
#include <obexftp/uuid.h>
#include <obexftp/cache.h>
#include <obexftp/obexftp_io.h>

#include <openobex/obex.h>
#include <openobex/obex_const.h>

#include "ositech_obex.h"
#include "ositech_communication.h"
#include "config.h"
#include "ositech_bt.h"
#include "sdp_op.h"

#define FTP_ARG_BUFF_SIZE	512
#define ERROR_RECV	-1

#define STREAM_CHUNK 4096
#define ONE_SECOND 	1

typedef struct pthread_arg {
	obexftp_client_t *client;
	int sockfd;
	int thread_exit;
} dir_arg;

typedef struct pthread_timer_arg {
	obexftp_client_t **client;
//	uint8_t count_down;
	uint timeout;
	uint8_t timer_exit;
	int led_org;
	sem_t start_timer;
	sem_t stop_timer;
} timer_arg;

static int bt_data_activity;

static void ResetBTLED(union sigval sig) {
//	printf("ResetBTLED timeout\n");
	bt_data_activity = 0;
}

void *SetBTLedinDataActive(void *arg) {
	struct itimerspec itv;
	struct sigevent sev;
	timer_t bt_led_timer;
	int timeout_nanosec = 100000000;
	char reset_data[512] = {};
	int fd, rd_sz;
	int led_mode = BT_LED_DATA_ACTIVITY;
	int *thead_exit = (int *)arg;

	// set bt led into act mode
	SetBTLed(BT_LED_DATA_ACTIVITY);
	bt_data_activity = 1;
	usleep(75000);
	
	// create timer
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = ResetBTLED;
	sev.sigev_notify_attributes = NULL;
	if(timer_create(CLOCK_REALTIME, &sev, &bt_led_timer) < 0) {
		perror("SetBTLedinDataActive: timer_create() failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: timer_create() failed.\n", __FUNCTION__);
		goto end;
	}

	// start timer
	itv.it_value.tv_sec = 0;
	itv.it_value.tv_nsec = timeout_nanosec;
	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_nsec = 0;

	if(timer_settime(bt_led_timer, 0, &itv, NULL) < 0) {
		perror("SetBTLedinDataActive: timer_settime() failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s(%d): timer_settime() failed.\n", __FUNCTION__, __LINE__);
		goto end1;
	}

	if((fd = open(BT_LED_STATE_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
		perror("SetBTLedinDataActive: open() failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: open() failed.\n", __FUNCTION__);
		goto end1;
	}



	while(*thead_exit == 0) {
		rd_sz = read(fd, reset_data, sizeof(reset_data));
		// if BT date act string is received then reset the timer to keep the BT led running in data act mode.
		if (rd_sz) {
			if (!bt_data_activity) {
				bt_data_activity = 1;
			}
			itv.it_value.tv_sec = 0;
			itv.it_value.tv_nsec = timeout_nanosec;
			if(timer_settime(bt_led_timer, 0, &itv, NULL) < 0) {
				perror("SetBTLedinDataActive: timer_settime() failed");
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s(%d): timer_settime() failed.\n", __FUNCTION__, __LINE__);
				break;
			}
			rd_sz = 0;
		}
		// if BT date act string is not received then the timer will be expired, and  the BT led back to solid
		if(!bt_data_activity && led_mode == BT_LED_DATA_ACTIVITY) {
			SetBTLed(BT_LED_SOLID);
			led_mode = BT_LED_SOLID;
		} else if(bt_data_activity && led_mode == BT_LED_SOLID) {
			SetBTLed(BT_LED_DATA_ACTIVITY);
			led_mode = BT_LED_DATA_ACTIVITY;
		} 
	}

	close(fd);
	
end1:
	timer_delete(bt_led_timer);
end:
	// set bt led back to solid
	SetBTLed(BT_LED_SOLID);
	pthread_exit(NULL);
}

void *ObexTimer(void *argument) {
	timer_arg *ftp_timer = (timer_arg *)argument;
	obexftp_client_t *cli = *(ftp_timer->client);
//	uint timeout = ftp_timer->timeout;
//	uint8_t count = 0;
	int led_org = ftp_timer->led_org;
	struct timespec timer;
	int error;

	while (1) {
		clock_gettime(CLOCK_REALTIME, &timer);
  		timer.tv_sec += ftp_timer->timeout;
//		printf("Start Timer\n");
		if(sem_timedwait(&ftp_timer->stop_timer, &timer) < 0) {
			error = errno;
			if(error == ETIMEDOUT) {
				printf("Timeout! Release BT connection\n");
				ReleasBTConnection(cli);
				*(ftp_timer->client) = NULL;
				break;
			}
		} else {
			if(ftp_timer->timer_exit) {
//				printf("Terminate Timer\n");
				break;
			} else {
//				printf("Stop Timer\n");
				sem_wait(&ftp_timer->start_timer);
			}
		}
/*		if (count >= timeout) {
			printf("Timeout! Release BT connection\n");
			ReleasBTConnection(cli);
			*(ftp_timer->client) = NULL;
			break;
		}
		
		if(ftp_timer->count_down) {
//			if(!(count%10)) printf("Start Timer %d.\n", count);
			count += 1;
		} else {
//			printf("Stop Timer\n");
			count = 0;
		}
		
		sleep(ONE_SECOND);
*/	}
	
	SetBTLed(led_org);
//	printf("Exit From %s\n", __FUNCTION__);
	pthread_exit(NULL);
}

/*********************************************************************** 
* Description:
* Convert the integer to string.
* 
* Calling Arguments: 
* Name			Description 
* value		the integer needs to be converted
* p 			the converted string of the integer
*
* Return Value: 
* none
******************************************************************************/
static void Int2String(int value, char *p) {
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

/* connect with given uuid. re-connect every time */
/*********************************************************************** 
* Description:
* connect to the device with the given uuid
* 
* Calling Arguments: 
* Name			Description 
* device		device information of the remote bluetooth adaptor
* channel		the channel of the FTP service.
* client		pointer to contain the connection infomation
* uuid		the string of the uuid
* uuid_len	the length of the uuid
*
* Return Value: 
* 0: error
* 1: success
******************************************************************************/
static int cli_connect_uuid(const char *device, const int channel, unsigned char **client, const uint8_t *uuid, int uuid_len)
{
	int retry;
	int res;
	obexftp_client_t *cli = NULL;

	/* Open */
	cli = obexftp_open (OBEX_TRANS_BLUETOOTH, NULL, NULL, NULL);
	if(cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s Error: obexftp_open() Failed\n", __FUNCTION__);
		fprintf(stderr, "Error opening obexftp-client\n");
		return 0;	//return FALSE;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: obexftp_open() SUCCESS\n", __FUNCTION__);

	for (retry = 0; retry < 3; retry++) {
		/* Connect */
		if ((res = obexftp_connect_uuid (cli, device, channel, uuid, uuid_len)) >= 0) {
			*client = (unsigned char *)cli;
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s obexftp_connect_uuid done and success\n", __FUNCTION__);
       		return 1;
		}
	
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s obexftp_connect_uuid returns res = %d. Try again.\n", __FUNCTION__, res);
		fprintf(stderr, "Still trying to connect\n");
	}

	obexftp_close(cli);
	cli = NULL;

	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s obexftp_connect_uuid failed\n", __FUNCTION__);
	return 0;
}

/* connect, possibly without fbs uuid. won't re-connect */
/*********************************************************************** 
* Description:
* start the OBEX connection with the given device
* 
* Calling Arguments: 
* Name			Description 
* device		device information of the remote bluetooth adaptor
* channel		the channel of the FTP service.
* client		pointer to contain the connection infomation
*
* Return Value: 
* -1: error
* 1: success
******************************************************************************/
static int CliConnect(const char *device, const int channel, unsigned char **client)
{
	const uint8_t *use_uuid = UUID_FBS;
	int use_uuid_len = sizeof(UUID_FBS);

	if (!cli_connect_uuid(device, channel, client, use_uuid, use_uuid_len))
//	if (!cli_connect_uuid(device, channel, client))	
		return -1;

	return 1;
}

/*********************************************************************** 
* Description:
* Disconnect the OBEX connection from the connected device
* 
* Calling Arguments: 
* Name			Description 
* cli		pointer to contain the connection infomation
*
* Return Value: 
* none
******************************************************************************/
static void CliDisconnect(obexftp_client_t *cli)
{
	int res;
	if (cli != NULL) {
		printf("Disconnecting...\n");
		/* Disconnect */
		res = obexftp_disconnect (cli);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: obexftp_disconnect() returns %d\n", __FUNCTION__, res);
		/* Close */
		obexftp_close (cli);
	}
}

//static obex_object_t *ObexBuildGet (obex_t obex, uint32_t conn, const uint8_t *name, const char *type)
static obex_object_t *ObexBuildGet (obex_t *obex, uint32_t conn, const uint8_t *name, const char *type)
{
	obex_object_t *object = NULL;
        uint8_t *ucname;
        int ucname_len;

        object = OBEX_ObjectNew(obex, OBEX_CMD_GET);
        if(object == NULL)
                return NULL;

        if(conn != 0xffffffff)
		(void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_CONNECTION, (obex_headerdata_t) conn, sizeof(uint32_t), OBEX_FL_FIT_ONE_PACKET);
 
        if(type != NULL) {
		// type header is a null terminated ascii string
		(void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_TYPE, (obex_headerdata_t) (const uint8_t *) type, strlen(type)+1, OBEX_FL_FIT_ONE_PACKET);
	}	
 
	if (name != NULL) {
		ucname_len = strlen((char *)name)*2 + 2;
		ucname = malloc(ucname_len);
		if(ucname == NULL) {
	                (void) OBEX_ObjectDelete(obex, object);
		        return NULL;
		}

		ucname_len = OBEX_CharToUnicode(ucname, name, ucname_len);

		(void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_NAME, (obex_headerdata_t) (const uint8_t *) ucname, ucname_len, OBEX_FL_FIT_ONE_PACKET);
		free(ucname);
	}
	
	return object;
}

/*********************************************************************** 
* Description:
* Create an OBEX object according to the given device information
*
* Return Value: 
* obex_object_t 	pointer to the obex_object
******************************************************************************/
//static obex_object_t *ObexBuildObj (obex_t obex, uint32_t conn, const char *name, const int size)
static obex_object_t *ObexBuildObj (obex_t *obex, uint32_t conn, const char *name, const int size)
{
	obex_object_t *object = NULL;
	uint8_t *ucname;
	int ucname_len;
	
	object = OBEX_ObjectNew(obex, OBEX_CMD_PUT);
	if(object == NULL)
		return NULL;

        if(conn != 0xffffffff)
		(void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_CONNECTION, (obex_headerdata_t) conn, sizeof(uint32_t), OBEX_FL_FIT_ONE_PACKET);

	ucname_len = strlen(name)*2 + 2;
	ucname = malloc(ucname_len);
	if(ucname == NULL) {
       	(void) OBEX_ObjectDelete(obex, object);
		return NULL;
	}

	ucname_len = OBEX_CharToUnicode(ucname, (uint8_t *)name, ucname_len);

	(void ) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_NAME, (obex_headerdata_t) (const uint8_t *) ucname, ucname_len, 0);
	free(ucname);
	
	
	if(size) (void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_LENGTH, (obex_headerdata_t) (uint32_t)size, sizeof(uint32_t), 0);

	
	(void) OBEX_ObjectAddHeader(obex, object, OBEX_HDR_BODY,
				(obex_headerdata_t) (const uint8_t *) NULL,
				0, OBEX_FL_STREAM_START);

	return object;
}

#define REMOTE_FILENAME_LENGTH	512
static obex_object_t *CreateObexObj_PUT(obexftp_client_t *cli, const char *filename, const int method) {
	obex_object_t *object;
	int fd, file_size = 0, cur_pos;
	char remotename[REMOTE_FILENAME_LENGTH] = {};
	char *psplit = NULL;
	
	if (cli == NULL)
		return NULL;
	
	if (cli->out_data) {
		printf("%s: Warning: buffer still active?\n", __func__);
	}
	
	if(method == FTPFROMFILE) {
		if((fd = open(filename, O_RDONLY)) < 0)
			return NULL;
		cur_pos = lseek(fd, 0, SEEK_CUR);
		file_size = lseek(fd, 0, SEEK_END);
		lseek(fd, cur_pos, SEEK_SET);
	}
	psplit = strrchr(filename, '/');
	if(psplit)
		snprintf(remotename, sizeof(remotename), "%s", psplit+1);
	else
		snprintf(remotename, sizeof(remotename), "%s", filename);
	
	object = ObexBuildObj(cli->obexhandle, cli->connection_id, remotename, file_size);
	
	return object;
}

#define XOBEX_LISTING "x-obex/folder-listing"

static obex_object_t *CreateObexObj_DIR(obexftp_client_t *cli) {
	obex_object_t *object;
	
	if (cli == NULL)
		return NULL;
	
	cli->infocb(OBEXFTP_EV_SENDING, NULL, 0, cli->infocb_data);
	object = ObexBuildGet(cli->obexhandle, cli->connection_id, NULL, XOBEX_LISTING);
	return object;
}

/*********************************************************************** 
* Description:
* create the socket for the MRx specified ftp connection.
*
* Calling Arguments: 
* Name			Description 
* cli		pointer to contain the connection infomation
* sockfd	socket id used to transfer the information between the bluetooth devices.
*
* Return Value: 
* -1: error
* 0: success
******************************************************************************/
static int ObexftpfromSocket(obexftp_client_t *cli, const int sockfd)
{
	uint8_t *fd = (uint8_t *)malloc(sizeof(uint8_t));
	if(fd == NULL) {
		printf("%s: malloc failed\n", __FUNCTION__);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - malloc() failed\n", __FUNCTION__);
		return -1;
	}
// if only the "fd" is set, then the api called is cli_fillstream_from_file;
// if only the "out_data" is set, then the api called is cli_fillstream_from_memory;
// if both the "fd" and "out_data" are set and equal to each other, then the api called is cli_fillstream_from_socket;
	cli->fd = sockfd;
	
	cli->out_data = fd;
	*fd = (uint8_t)sockfd;
/*	printf("cli->outdata(%p) %d %d %d %d\n",cli->out_data,
								*cli->out_data, 
								*(cli->out_data+1),
								*(cli->out_data+2),
								*(cli->out_data+3));
*/
	cache_purge(&cli->cache, NULL);

	return 0;
}

static int ObexftpfromFile(obexftp_client_t *cli, const char *filename)
{
// if only the "fd" is set, then the api called is cli_fillstream_from_file;
// if only the "out_data" is set, then the api called is cli_fillstream_from_memory;
// if both the "fd" and "out_data" are set and equal to each other, then the api called is cli_fillstream_from_socket;
	if((cli->fd = open(filename, O_RDONLY, 0))<= 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - open() failed %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}
	
	cli->out_data = NULL;

	cache_purge(&cli->cache, NULL);

	return 0;
}
/*********************************************************************** 
* Description:
* release the created Obexftp connection.
*
* Calling Arguments: 
* Name			Description 
* cli		pointer to contain the connection infomation
*
* Return Value: 
* none	
******************************************************************************/
static void ReleaseObexftpSocket(obexftp_client_t *cli) {
	uint8_t *fd = (uint8_t *)cli->out_data;

	cli->out_data = NULL;
	free(fd);
}


/*
 * Function AbortCmd()
 *
 *    Abort an ongoing request
 *
 */
static int AbortCmd(obex_t *self)
{
	OBEX_CancelRequest(self, 1);	

	return 1;	
}

// 1: abort received
// 0: no abort received
static void *RecvAbort(void *para) {
	char cmd_string[64];
	char up_cmd[64];
	int cmd_leng;
	int res, cmd, ret = 0;
	dir_arg *arg = (dir_arg *)para;
	int sockfd = arg->sockfd;
	obexftp_client_t *cli = arg->client;
	fd_set read_set;
	struct timeval timeout;
	
	printf("%s: Only ABORT is allowed\n", __FUNCTION__);
	
	while(1) {
		FD_ZERO(&read_set);
		FD_SET(sockfd, &read_set);
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
	
		if((res = select(FD_SETSIZE, &read_set, NULL, NULL, &timeout))< 0) {
			perror("RecvAbort(): select()");
			break;
		} else if (res > 0) {
			if (FD_ISSET(sockfd, &read_set)) {
				memset(cmd_string, 0, sizeof(cmd_string));
				read(sockfd, cmd_string, sizeof(cmd_string));
		
				// get rid of '\n'
				if ((cmd_leng = strlen(cmd_string)) > 0) {
					cmd_leng--;
					if (cmd_string[cmd_leng] == '\n')  cmd_string[cmd_leng] = '\0';
				}
				
				memset(up_cmd, 0, (cmd_leng+1)*sizeof(char));
				String2Upper(up_cmd, cmd_string);
				cmd = GetFTPCMD(up_cmd, NULL, NULL);
				if(cmd == BT_FTP_ABORT) {
					printf("Calling AbortCmd\n");
					if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - Calling AbortCmd\n", __FUNCTION__);
					AbortCmd(cli->obexhandle);
					ret = 1;
					break;
				} else 
					printf("Error: only ABORT is accepted within the FTP DIR -RAW\n");
			}
		} else {
			if (arg->thread_exit)
				break;
		}
	}
	
//	printf("%s: return\n", __FUNCTION__);
// 	change dir to the current dir in order to solve the put failure issue after the abort.
	pthread_exit ((void *)ret);
}
/*********************************************************************** 
* Description:
* send the request of the obex synchronization.
*
* Calling Arguments: 
* Name			Description 
* cli		pointer to contain the connection infomation
*
* Return Value: 
* 1: success
* < 0: faile
******************************************************************************/
static int ObexftpSync(obexftp_client_t *cli)
{
	int ret;
	
	while(!cli->finished) {
		ret = OBEX_HandleInput(cli->obexhandle, 20);
		
		if (ret <= 0) {
			printf("%s() OBEX_HandleInput = %d\n", __func__, ret);
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: OBEX_HandleInput returns ret = %d\n", __FUNCTION__, ret);
			return -1;
		}
	}

//	printf("%s() Done success=%d\n", __func__, cli->success);
//	printf("%s() Finished=%d\n", __func__, cli->finished);  
	if(cli->success)
		return 1;
	else
		return 0;
}

static int SendObexRequest(obexftp_client_t *cli, obex_object_t *object) {
	int res;
	int led_thread_exit = 0;
	pthread_t led_thread_id;
	
	if (!cli->finished) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli->finished %d\n", __FUNCTION__, cli->finished);
		return -EBUSY;
	}
	cli->finished = 0;

	// create a fifo
/*	if (!access(BT_LED_STATE_FIFO, F_OK))
		unlink(BT_LED_STATE_FIFO);
	
	if(mkfifo(BT_LED_STATE_FIFO, 0777) < 0) {
		perror("SetBTLedinDataActive: mkfifo() failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: mkfifo() failed.\n", __FUNCTION__);
		return -1;
	}
	
	pthread_create(&led_thread_id, NULL, SetBTLedinDataActive, (void *)&led_thread_exit);
*/	
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: calling OBEX_Request\n", __FUNCTION__);
	(void) OBEX_Request(cli->obexhandle, object);

	res = ObexftpSync (cli);
	led_thread_exit = 1;

/*	pthread_join(led_thread_id, NULL);
	
	if (!access(BT_LED_STATE_FIFO, F_OK))
		unlink(BT_LED_STATE_FIFO);
*/
	return res;
}

/*********************************************************************** 
* Description:
* start the file transmission
*
* Calling Arguments: 
* Name			Description 
* cli		pointer to contain the connection infomation
* filename 	the name of the sending file
* sockfd		the socket id of the socket being used to send the file if it's given otherwise the 
*			file transmission will be used
*
* Return Value: 
* >=0: bytes of sending out
* < 0: fail
******************************************************************************/
int FTPTransFile(obexftp_client_t *cli, const char *filename, const int method, const int sockfd) {
	int res;
	obex_object_t *obj = CreateObexObj_PUT(cli, filename, method);

	if (cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli is NULL\n", __FUNCTION__);
		return -1;
	}
	
	if (!obj) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - Create obex_object_t failed.\n", __FUNCTION__);
		return -1;
	}

	if(method == FTPFROMSOCKET){
		if(ObexftpfromSocket(cli, sockfd) < 0) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - set FTP from socket failed.\n", __FUNCTION__);
			return -1;
		}
	} else if(method == FTPFROMFILE){
		if(ObexftpfromFile(cli, filename) < 0) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - set FTP from file failed.\n", __FUNCTION__);
			return -1;
		}
	}
	
	res = SendObexRequest(cli, obj);
	
	if(method == FTPFROMSOCKET) ReleaseObexftpSocket(cli);
	return res;
}

/*********************************************************************** 
* Description:
* create or change directory on the remote bluetooth adaptor
*
* Calling Arguments: 
* Name			Description 
* cli			pointer to contain the connection infomation
* name 		the name of the directory
* create		the action of creating the directory or not
*
* Return Value: 
* < 0: error
* 0: fail
* 1: success
******************************************************************************/
static int SetDir(obexftp_client_t *cli, const char *name, const int create) {
	obex_object_t *object;
	int res;
	int name_leng = strlen(name)*sizeof(char);
	char token[] = "/";
	char *ptmp = NULL;
	char *pname = (char *)malloc(name_leng);

	if(!pname) {
		printf("Error: %s(%d) malloc()\n", __FUNCTION__, __LINE__);
		res = -1;
		goto end;
	}
	memset(pname, 0, name_leng);
	snprintf(pname, name_leng+1, "%s", name);

	if (!strcmp(pname, "\\")) 
		memset(pname, 0, name_leng);
		
	ptmp = strtok(pname, token);
	if (!ptmp) {
//		cli->infocb(OBEXFTP_EV_SENDING, pname, 0, cli->infocb_data);
		printf("%s() Setpath \"%s\"\n", __func__, pname);
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Setpaht \"%s\"\n", __FUNCTION__, pname);
		object = obexftp_build_setpath(cli->obexhandle, cli->connection_id, pname, create);
		res =  SendObexRequest(cli, object);
	} else {
		do {
//			cli->infocb(OBEXFTP_EV_SENDING, ptmp, 0, cli->infocb_data);
			printf("%s() Setpath \"%s\"\n", __func__, ptmp);
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Setpaht \"%s\"\n", __FUNCTION__, pname);
			object = obexftp_build_setpath(cli->obexhandle, cli->connection_id, ptmp, create);
			res =  SendObexRequest(cli, object);
			if (res <= 0)
				break;
		} while((ptmp = strtok(NULL, token)));
	}
	
	free(pname);

end:	
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s returns res = %d.\n", __FUNCTION__, res);
	return res;
}

// 1: done
// -1: error
int ChangeDir(obexftp_client_t *cli, const char *name) {
	if (cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli is NULL\n", __FUNCTION__);
		return -1;
	}

	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: calling SetDir()\n", __FUNCTION__);
	return SetDir(cli, name, 0);
}

// 1: done
// -1: error
int MakeDir(obexftp_client_t *cli, const char *name) {
	if (cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli is NULL\n", __FUNCTION__);
		return -1;
	}

	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: calling SetDir()\n", __FUNCTION__);
	return SetDir(cli, name, 1);
}

// 1: success
int ListDir(obexftp_client_t *cli) {
	int res;
	obex_object_t *obj = NULL;

	
	if (cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli is NULL\n", __FUNCTION__);
		return -1;
	}

	if (cli->buf_data) {
		printf("%s: Warning: buffer still active?\n", __func__);
	}
	
	cli->target_fn = NULL;
	
	obj = CreateObexObj_DIR(cli);
	if (!obj) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - Create obex_object_t failed.\n", __FUNCTION__);
		return -1;
	}

	res = SendObexRequest(cli, obj);
	
	return res;
}

#define LISTFOLDER_XML	"/tmp/bt_ftp_dir.xml"
/* if the DIR-RAW command is issued, create the "bt_ftp_dir.xml" file for storing the folder listing */
int CreateDirXML(void) {
	int fd = open(LISTFOLDER_XML, O_RDWR | O_CREAT | O_TRUNC);
	if (fd < 0) {
		perror("CreateDirXML open");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s open() failed\n", __FUNCTION__);
		return 0;
	}

//	printf("Create %s successed\n", LISTFOLDER_XML);
	close(fd);
	return 1;
}

/* once the folder listing is sent, delete the .xml file */
void DelDirXML(void) {
	unlink(LISTFOLDER_XML);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Delete %s\n", __FUNCTION__, LISTFOLDER_XML);
}


int GetDirXML(const int sockfd, const int display) {
	char *buf = NULL;
	int file_size = 0;
	int cur_pos;
	int buf_len;
	const char tok[2] = "\n";
	char *pstring = NULL;
	
	int fd = open(LISTFOLDER_XML, O_RDONLY);
	if (fd < 0)
		return 0;

	cur_pos = lseek(fd, 0, SEEK_CUR);
	file_size = lseek(fd, 0, SEEK_END);
	lseek(fd, cur_pos, SEEK_SET);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: %s file_size = %d\n", __FUNCTION__, LISTFOLDER_XML, file_size);
	if (file_size > 0) {
		file_size += 1;
		buf = (char *)malloc(file_size*sizeof(char));
		if (!buf) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Error -- SendFolderListing malloc()\n", __FUNCTION__);
			perror("SendFolderListing malloc()");
			return 0;
		}
		memset(buf, 0, file_size*sizeof(char));
	} else
		return 0;

	buf_len = read(fd, buf, file_size*sizeof(char));
//	printf("file size %d\n", buf_len);
	pstring = strtok(buf, tok);
	if(pstring) {
		if(display == DISPLAY_DIR_XML) printf("%s\n", pstring);
		if(sockfd >= 0) SendResponse(sockfd, pstring);
		while((pstring =  strtok(NULL, tok))) {
			if(display == DISPLAY_DIR_XML) printf("%s\n", pstring);
			if(sockfd >= 0) SendResponse(sockfd, pstring);
		}
	}

	free(buf);
	close(fd);

	return 1;
}

// 1: success;
// 0: being abort;
// -1: error
static int GetDirContent(obexftp_client_t *cli, const int sockfd) {
	dir_arg arg;
	pthread_t child_thread_id;
	void *ret;
	int res;
	
	memset(&arg, 0, sizeof(dir_arg));
	arg.sockfd = sockfd;
	arg.client = cli;
	arg.thread_exit = 0;

	if(pthread_create(&child_thread_id, NULL, RecvAbort, (void *)&arg) < 0) {
		perror("ListDir(): pthread_create()");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - pthread_create() failed.\n", __FUNCTION__);
		return -1;
	}
	
	res = ListDir(cli);
	arg.thread_exit = 1;

	if(pthread_join(child_thread_id, &ret) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - pthread_join() failed.\n", __FUNCTION__);
		perror("ListDir(): pthread_join()");	
	}
	// if ret = 0, then no ABORT is received
	// if ret = 1, then there is an ABORT is received.
	
	if ((int)ret == 1) {
		ChangeDir(cli, ".");
		res = 0;
	}	
	else
		res = 1;
	
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s returns %d\n", __FUNCTION__, res);
	return res;
}

int EstablisBTConnection(const char *device, const int channel, unsigned char **client) {
	printf("Connecting...\n");
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Connecting...\n", __FUNCTION__);
	return CliConnect(device, channel, client) ;
}

void ReleasBTConnection(obexftp_client_t *cli) {	
	if (cli == NULL) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: cli is NULL\n", __FUNCTION__);
		return;
	}
	CliDisconnect(cli);
}

/*********************************************************************** 
* Description:
* search the obex service on the given remote device.
*
* Calling Arguments: 
* Name			Description 
* addr		the address of the given remote device
* res_channel		found device OBEX channel
*
* Return Value: 
* < 0: error
* 1: success
******************************************************************************/
/*
int SearchBTwithObex(const char *addr, int *res_channel)
{
  	bdaddr_t bdaddr;
	int channel = -1;
	int obex_profiles[2] = {OBEX_FILETRANS_SVCLASS_ID, OBEX_OBJPUSH_SVCLASS_ID};
	int profile;

  	sdp_list_t *attrid, *search, *seq, *loop;
  	uint32_t range = SDP_ATTR_PROTO_DESC_LIST;
  
  	sdp_session_t *sess;
  	struct hci_dev_info di;
  	uuid_t root_uuid;

  	if (!addr || strlen(addr) != 17) {
		printf("Invalid BT device address.\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Given BT device %s is invalid. Return ERROR 01\n", __FUNCTION__, addr);
		return ERROR;
    	} else
		str2ba(addr, &bdaddr);

	// Get local bluetooth address
	if(hci_devinfo(0, &di) < 0) {
      		perror("HCI device info failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Get HCI device info failed.  Return ERROR 01\n", __FUNCTION__);
      		return ERROR;
    	} 
	// Connect to remote SDP server
	sess = sdp_connect(&di.bdaddr, &bdaddr, SDP_RETRY_IF_BUSY);
 	if(!sess) {
      		fprintf(stderr, "Failed to connect to the SDP server\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Failed to connect to the SDP server of the remote BT device. Return BTDOWN\n", __FUNCTION__);
      		return NO_CARRIER;
    	}
	
	str2ba(addr, &bdaddr);
	printf("Browsing BT %s ...\n", addr);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Browsing BT %s ...\n", __FUNCTION__, addr);
  	// Build linked lists for OBEX profiles
  	for(profile = 0; profile < sizeof(obex_profiles) / sizeof(int); profile++) {
		sdp_uuid16_create(&root_uuid, obex_profiles[profile]);
		// Get a linked list of services
  		attrid = sdp_list_append(0, &range);
  		search = sdp_list_append(0, &root_uuid);
		if(sdp_service_search_attr_req(sess, search, SDP_ATTR_REQ_RANGE, attrid, &seq)) {
      			perror("OBEX Service search failed");
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: %s service search Failed.\n", __FUNCTION__, (profile==0) ? "FTP" : "OPUSH");
			sdp_close(sess);
      			return NO_CARRIER;
		}
		sdp_list_free(attrid, 0);
  		sdp_list_free(search, 0);

		// Loop through the list of services
  		for(loop = seq; loop; loop = loop->next) {
     			sdp_record_t *rec = (sdp_record_t *) loop->data;
      			sdp_list_t *access = NULL;
      		
      			// Print the RFCOMM channel
      			sdp_get_access_protos(rec, &access);
 			if(access){
	  			channel = sdp_get_proto_port(access, RFCOMM_UUID);
	  			printf("Using Channel: %d\n", channel);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Find %s service on Channel %d.\n", __FUNCTION__, (profile==0) ? "FTP" : "OPUSH", channel);
	  			*res_channel = channel;
				goto done;
			} else {
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s:  NO %s service found.\n", __FUNCTION__, (profile==0) ? "FTP" : "OPUSH");
				sdp_list_free(seq, 0);	
			}
    		}
  	}

done:
    	sdp_list_free(seq, 0);
    	sdp_close(sess);

	if (channel > 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: OBEX service search Done and Success\n", __FUNCTION__);
	    	return 1;
	}else {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: OBEX service search Failed\n", __FUNCTION__);
		return NO_CARRIER;
	}
}
*/
int SearchBTwithObex(const char *addr, int *res_channel)
{
	int obex_profiles[2] = {OBEX_FILETRANS_SVCLASS_ID, OBEX_OBJPUSH_SVCLASS_ID};
	int index;
	int res;
	
	for(index = 0; index < sizeof(obex_profiles) / sizeof(int); index++) {
		if((res = GetProfileChannel(addr, obex_profiles[index], res_channel)) == 1)
			break;
	}

	return res;
}
/*********************************************************************** 
* Description:
* the loop of receiving FTP command from the MRx
*
* Calling Arguments: 
* Name			Description 
* cli_sockfd		the socket id of the connection used to send the FTP command
* client 			pointer to contain the connection infomation
*
* Return Value: 
* 1: success
* 
* Note: If there is no connection for a minute, the FTP session should be quit. A DISCONNECT
* OBEX request would be sent to the remote BT device to shut down the current connected FTP session.
******************************************************************************/
int StartFTPSession(const int cli_sockfd, unsigned char *client, const uint inactive_timeout, const int led_org) {
	int cmd = -1;
	int ftp_quit = 0;
	int ftp_res;

	char arg[FTP_ARG_BUFF_SIZE] = {};
	obexftp_client_t *cli = (obexftp_client_t *)client;
	pthread_t timer_thread_id;
	timer_arg ftp_timer;
	
	printf("Start FTP session\n");
	SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
	printf("------------\n");

	ftp_timer.timeout = inactive_timeout;	// two mins
	ftp_timer.timer_exit = 0;
	ftp_timer.client = &cli;
	ftp_timer.led_org = led_org;
	sem_init(&ftp_timer.start_timer, 0, 0);
	sem_init(&ftp_timer.stop_timer, 0, 0);

	if(pthread_create(&timer_thread_id, NULL, ObexTimer, (void *)&ftp_timer) < 0) {
		perror("StartFTPSession(): pthread_create()");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - pthread_create() failed.\n", __FUNCTION__);
		return -1;
	}
	
	while((cmd = RecvCmd(cli_sockfd, arg, 1)) > 0) {
		switch (cmd) {
			case BT_FTP_CD:
				printf("CD %s\n", arg);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - CD.\n", __FUNCTION__);
				sem_post(&ftp_timer.stop_timer);
				ftp_res = ChangeDir(cli, arg);
				sem_post(&ftp_timer.start_timer);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: ChangeDir() returns ftp_res = %d\n", __FUNCTION__, ftp_res);
				if (ftp_res > 0)
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
				else
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_NOT_FOUND);
				break;
			case BT_FTP_MD:
				printf("MD %s\n", arg);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - MD.\n", __FUNCTION__);
				sem_post(&ftp_timer.stop_timer);
				ftp_res = MakeDir(cli, arg);
				sem_post(&ftp_timer.start_timer);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: MakeDir() returns ftp_res = %d\n", __FUNCTION__, ftp_res);
				if (ftp_res > 0)
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
				else if (ftp_res < 0)
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_INTERNAL_SERVER_ERROR);
				else
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_UNAUTHORIZED);
				break;
			case BT_FTP_GET_MAX:
				{
					char resp[16]= {};
					Int2String(STREAM_CHUNK, resp);
					
					printf("MAX MTU: %s\n", resp);
					if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - MAX.\n", __FUNCTION__);
					SendResponse(cli_sockfd, resp);
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
					break;
				}
			case BT_FTP_PUT:
				{
					if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - PUT.\n", __FUNCTION__);
					printf("Start to transmit file [%s]\n", arg);
					SendResponse(cli_sockfd, "!");
					sem_post(&ftp_timer.stop_timer);
					ftp_res = FTPTransFile(cli, arg, FTPFROMSOCKET, cli_sockfd);
					sem_post(&ftp_timer.start_timer);
					if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTPTransFile() returns ftp_res = %d\n", __FUNCTION__, ftp_res);
					/* 
					When the FTP is started, the socket is read by the openobex layer. However,
					if any exceptions occured before or during the FTP, the openobex layer is existed,
					and the sending msg from the MRx in the socket should be by the application before
					sending back the response.
					*/
					if(ftp_res <= 0 && cli) {
						// read the rest msg in the socket if error.
						char buff[BUFFER_SIZE] = {};
						memset(buff, 0, sizeof(buff));
						
						RecvSocketMsg(cli->fd, buff, sizeof(buff));	
					}
					if (ftp_res > 0)
						SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
					else if (ftp_res < 0)
						SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_INTERNAL_SERVER_ERROR);
					else
						SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_UNAUTHORIZED);
					break;
				}
			case BT_FTP_DIR:		
			/*
				if the DIR-RAW is being abort, then FTP response is regarding
				to the ABORT cmd instead of the DIR-RAW
			*/
				CreateDirXML();
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - DIR -RAW.\n", __FUNCTION__);
				printf("Get folder listing\n");
				sem_post(&ftp_timer.stop_timer);
				//ftp_res =ListDir(cli, cli_sockfd);
				ftp_res = GetDirContent(cli, cli_sockfd);
				sem_post(&ftp_timer.start_timer);
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: ListDir() returns ftp_res = %d\n", __FUNCTION__, ftp_res);
				if (ftp_res > 0) {
					GetDirXML(cli_sockfd, 0);
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
					DelDirXML();
				} else if (ftp_res < 0)
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_UNACCEPTABLE);
				else
					SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
				
				break;
			case BT_FTP_QUIT:
				ftp_quit = 1;
				ftp_timer.timer_exit = 1;
				sem_post(&ftp_timer.stop_timer);
				if(pthread_join(timer_thread_id, NULL) < 0) {
					debuglog(LOG_INFO, "[libositech_obex.so] Error: %s - pthread_join() failed.\n", __FUNCTION__);
					perror("StartFTPSession(): pthread_join()");	
				}
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - QUIT.\n", __FUNCTION__);
				printf("Quit FTP session\n");
				if(cli) {
					ReleasBTConnection(cli);
					cli = NULL;
				}
				SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
				break;
			case BT_FTP_ABORT:
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command - ABORT.\n", __FUNCTION__);
				SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_SUCCESS);
				break;
			default:	
				if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: FTP command is unknown.\n", __FUNCTION__);
				printf("Unknown FTP command\n");
				SendFTPResponse(cli_sockfd, BT_FTP_SERVICE_UNACCEPTABLE);
				break;
		}
		
		if(ftp_quit) {
			printf("FTP transmission complete, shutting down connection...\n");
			break;
		}
		printf("------------\n");
		memset(arg, 0, sizeof(arg));
	}
	
	sem_destroy(&ftp_timer.start_timer);
	sem_destroy(&ftp_timer.stop_timer);
	return 1;
}


