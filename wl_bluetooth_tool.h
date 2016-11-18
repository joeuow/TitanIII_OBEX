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
* File Name:			wl_bluetooth_tool.h
* Last Modified:
* Changes:
**********************************************************************/
#ifndef __WL_BT_H
#define __WL_BT_H

#include "config.h"

#define DEBUGLOG DEBUGLOG_ON
#define DEBUG	1

#define STRING_LENG	32
#define ADDR_LENG	STRING_LENG
#define PINCODE_LENG	STRING_LENG
#define DEVNAME_LENG	STRING_LENG*2
#define PROFILE_LENG	STRING_LENG*2
#define DEBUGMSG_LENG	STRING_LENG*4
#define READ_BUFF_LENG STRING_LENG*4
#define UUID_LENGTH 	STRING_LENG*2
#define CMD_LENG		STRING_LENG*4

#define WL_PINCODE_FILE	"/tmp/BT_pincode"
#define BT_PEERS_PROFILES_CSV_FILE		"/tmp/BT_peer_profiles.csv"
#define BT_TOOL_TEST_FILE	"/tmp/bt_test.txt"

struct BTdev_info {
	char dev_name[DEVNAME_LENG];
	char dev_addr[ADDR_LENG];
	uint8_t profile;	// number of supported profile.
	struct BTdev_info *pnext;
};

struct help_instruct {
	char *option;
	char *argument;
	char *desc;
};

struct local_service {
	char UUID[UUID_LENGTH];
	char option;
};

#endif