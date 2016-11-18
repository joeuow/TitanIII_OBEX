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
* File Name:			sdp_op.c
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

#include <debuglog.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <obexftp/obexftp.h>
#include <obexftp/client.h>
#include <obexftp/uuid.h>
#include <obexftp/cache.h>
#include <obexftp/obexftp_io.h>

#include <openobex/obex.h>

#include "ositech_obex.h"
#include "ositech_communication.h"
#include "config.h"
#include "ositech_bt.h"
#include "sdp_op.h"
#include "hci_info.h"

// -1, -2: error
// 1: success
static int SdpConnect(sdp_session_t **sess, const char *addr) {
	bdaddr_t local_addr;
	bdaddr_t bdaddr;

  	if (!addr || strlen(addr) != 17) {
		printf("Invalid BT device address.\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Given BT device %s is invalid. Return ERROR 01\n", __FUNCTION__, addr);
		return SDP_ERROR;
    	} else
		str2ba(addr, &bdaddr);

	// Get local bluetooth address
	if(!GetBTDevAdd(&local_addr)) {
//	if(hci_devinfo(0, &di) < 0) {
 //     		perror("HCI device info failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Get local BT address failed.  Return ERROR 01\n", __FUNCTION__);
      		return SDP_ERROR;
    	} 
	// Connect to remote SDP server
	*sess = sdp_connect(&local_addr, &bdaddr, SDP_RETRY_IF_BUSY);
 	if(!*sess) {
      		fprintf(stderr, "Failed to connect to the SDP server\n");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] Error %s: Failed to connect to the SDP server of the remote BT device. Return BTDOWN\n", __FUNCTION__);
      		return SDP_NO_CARRIER;
    	}

	return SDP_SUCCESS;
}

static void SdpClose(sdp_session_t *sess) {
	sdp_close(sess);
}

// 1: success
// <0: fail
static int SdpSearch(sdp_session_t *sess, int profile, sdp_list_t **seq) {
	sdp_list_t *attrid, *search;
	uint32_t range;		
	uuid_t root_uuid;

	// create uuid & range for the profile
	if(profile) {
		range = SDP_ATTR_PROTO_DESC_LIST;
		sdp_uuid16_create(&root_uuid, profile);
	} else {
		range = 0x0000ffff;	// SDP_ATTR_REQ_RANGE	
		sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	}
	
	// Get a linked list of services
  	attrid = sdp_list_append(0, &range);
  	search = sdp_list_append(0, &root_uuid);
	
	if(sdp_service_search_attr_req(sess, search, SDP_ATTR_REQ_RANGE, attrid, seq)) {
      		perror("Service search failed");
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: %s service search Failed.\n", __FUNCTION__, (profile==0) ? "FTP" : "OPUSH");
      		return SDP_NO_CARRIER;
	}
	sdp_list_free(attrid, 0);
  	sdp_list_free(search, 0);
	return SDP_SUCCESS;
}

// 1: success
// <0: fail
int SearchBTService(const char *addr, int profile, sdp_list_t **seq) {
  	sdp_session_t *sess;
	int error = 0;

	if((error = SdpConnect(&sess, addr)) < 0)
		return error;
	
	printf("Searching BT %s ...\n", addr);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Searching BT %s ...\n", __FUNCTION__, addr);

	error = SdpSearch(sess, profile, seq);
	SdpClose(sess);
	return error;
}

// 1: success
// <0: fail
int BrowseBTServices(const char *addr, sdp_list_t **seq)
{
  	sdp_session_t *sess;
	int error = 0;

	if((error = SdpConnect(&sess, addr)) < 0)
		return error;
	
	printf("Browsing BT %s ...\n", addr);
	if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Browsing BT %s ...\n", __FUNCTION__, addr);

	error = SdpSearch(sess, 0, seq);
	SdpClose(sess);
	return error;
}

// 1: success
// <0: fail
int GetProfileChannel(const char *addr, int profile, int *res_channel) {
	sdp_list_t *seq, *loop;
	int error = 0;
	int channel = -1;

	if((error=SearchBTService(addr, profile, &seq)) < 0)
		return error;
		// Loop through the list of services
  	for(loop = seq; loop; loop = loop->next) {
     		sdp_record_t *rec = (sdp_record_t *) loop->data;
      		sdp_list_t *access = NULL;
      		
      		// Get the RFCOMM channel
      		sdp_get_access_protos(rec, &access);
 		if(access){
	  		channel = sdp_get_proto_port(access, RFCOMM_UUID);
	  		printf("Using Channel: %d\n", channel);
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: Find %x service on Channel %d.\n", __FUNCTION__, profile, channel);
	  		*res_channel = channel;
			goto done;
		} else 
			if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s:  NO %x service found.\n", __FUNCTION__, profile);	
    	}


done:
    	sdp_list_free(seq, 0);

	if (channel > 0) {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: %x service search Done and Success\n", __FUNCTION__, profile);
	    	return 1;
	}else {
		if(debuglog_enable) debuglog(LOG_INFO, "[libositech_obex.so] %s: %x service search Failed\n", __FUNCTION__, profile);
		return SDP_NO_CARRIER;
	}
}
