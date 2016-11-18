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
* File Name:			sdp_op.h
* Last Modified:
* Changes:
**********************************************************************/
#ifndef __SDP_OP_H
#define __SDP_OP_H

#define SDP_ERROR	-1
#define SDP_NO_CARRIER	-2
#define SDP_SUCCESS		1

extern int GetProfileChannel(const char *addr, int profile, int *res_channel);
extern int BrowseBTServices(const char *addr, sdp_list_t **seq);
extern int SearchBTService(const char *addr, int profile, sdp_list_t **seq);
#endif