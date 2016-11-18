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
* File Name:			rfcomm_op.c
* Last Modified:
* Changes:
**********************************************************************/
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <termios.h>
#include <sys/poll.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <debuglog.h>
#include "ositech_obex.h"
#include "ositech_communication.h"
#include "config.h"
#include "ositech_bt.h"
#include "sdp_op.h"
#include "hci_info.h"
#include "rfcomm_op.h"

#define SERIAL_DEVNAME "/dev/rfcomm0"
/*********************************************************************** 
* Description:
* Open the Bluetootth RFCOMM socket.
* 
* Calling Arguments: 
* Name			Description 
* None
*
* Return Value: 
* int		socket id
******************************************************************************/
static int OpenRfcommSocket(void) {
	int sock_fd;
	
	sock_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	
	if (sock_fd < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: socket() failed and Can't open RFCOMM control socket\n", __FUNCTION__);
		perror("Can't open RFCOMM control socket");
		return -1;
	}
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: socket() SUCCESS\n", __FUNCTION__);

	return sock_fd;
}

static void SetRfcomm(struct sockaddr_rc *laddr, bdaddr_t *local_addr, struct sockaddr_rc *raddr, const char *remot_addr, const int channel) {
	laddr->rc_family = AF_BLUETOOTH;
	bacpy(&laddr->rc_bdaddr, local_addr);
	laddr->rc_channel = 0;	

	raddr->rc_family = AF_BLUETOOTH;
	str2ba(remot_addr, &raddr->rc_bdaddr);
	raddr->rc_channel = channel;
}
/*********************************************************************** 
* Description:
* Close a Bluetooth RFCOMM socket.
* 
* Calling Arguments: 
* Name		Description 
* int		socket id
*
* Return Value: 
* None
******************************************************************************/
static void CloseRfcommSocket(const int ctl) {
	close(ctl);
}

// 0: success;
// -1 : failed
static int CreateTTYDev(int sk, bdaddr_t *local, bdaddr_t *remote, int channel, int *dev) {
	struct rfcomm_dev_req req;
	
	memset(&req, 0, sizeof(req));
	req.dev_id = GetBTDevID();
	req.flags = (1 << RFCOMM_REUSE_DLC) | (1 << RFCOMM_RELEASE_ONHUP);

	bacpy(&req.src, local);
	bacpy(&req.dst, remote);
	req.channel = channel;

	if((*dev = ioctl(sk, RFCOMMCREATEDEV, &req)) < 0)
		return -1;
	else
		return 0;
}

static void ReleaseTTYDev(int sk, int dev) {
	struct rfcomm_dev_req req;

	req.dev_id = dev;
	req.flags = (1 << RFCOMM_HANGUP_NOW);
	ioctl(sk, RFCOMMRELEASEDEV, &req);
	
	return ;
}

// 1: success
// < 0 :fail
int SearchBTwithSerial(const char *addr, int *res_channel)
{
	int serial_profile = SERIAL_PORT_SVCLASS_ID;
	
	return GetProfileChannel(addr, serial_profile, res_channel);
}

// fd: fd of the rfcomm0 success
// -1 :fail
int RfcommConnect(const char *rbt_addr, int channel) {
	bdaddr_t lbt_addr;
	struct sockaddr_rc laddr, raddr;
	struct termios ti;
	int sk, fd;
	int errno;
	int retry = 3;
	int res = -1;
	int dev = -1;

	GetBTDevAdd(&lbt_addr);
	SetRfcomm(&laddr, &lbt_addr, &raddr, rbt_addr, channel);

	sk = OpenRfcommSocket();
	if (bind(sk, (struct sockaddr *) &laddr, sizeof(laddr)) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Can't bind RFCOMM socket (%s)\n", strerror(errno));
		goto end;
	}

	if (connect(sk, (struct sockaddr *) &raddr, sizeof(raddr)) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Can't connect RFCOMM socket (%s)\n", strerror(errno));
		goto end;
	}
	if(CreateTTYDev(sk, &laddr.rc_bdaddr, &raddr.rc_bdaddr, channel, &dev) < 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Can't Create RFCOMM %s (%s)\n", SERIAL_DEVNAME, strerror(errno));
		goto end;
	}
	while ((fd = open(SERIAL_DEVNAME, O_RDWR | O_NOCTTY)) < 0) {
		if (errno == EACCES) {
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Can't open RFCOMM %s (%s)\n", SERIAL_DEVNAME, strerror(errno));
			goto end;
		}
		if (retry--) {
			usleep(100 * 1000);
			continue;
		}
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Can't open RFCOMM %s (%s)\n", SERIAL_DEVNAME, strerror(errno));
		goto end;
	}
	tcflush(fd, TCIOFLUSH);
	cfmakeraw(&ti);
	tcsetattr(fd, TCSANOW, &ti);

	res = fd;
end:
	CloseRfcommSocket(sk);
	return res;
}

void RfcommDisconnect(int sr_fd) {
	int sk = OpenRfcommSocket();
	int dev = GetBTDevID();

	close(sr_fd);
	if(sk >=0 && dev >0) {
		ReleaseTTYDev(sk, dev);
	}
}