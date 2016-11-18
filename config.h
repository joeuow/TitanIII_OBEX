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
* File Name:			config.h
* Last Modified:
* Changes:
**********************************************************************/
#ifndef _CONFIG_H
#define _CONFIG_H

#define CONFIG_FILE	"/mnt/flash/config/conf/bt_obex.conf"

// inactive.timeout
#define ONE_MINUTE	60
#define DEFAULT_INACTIVE_TIMEOUT	2*ONE_MINUTE

// debuglog.enable
#define DEBUGLOG_ON	1
#define DEBUGLOG_OFF	0
#define DEFAULT_DEBUGLOG_ENABLE	DEBUGLOG_ON

int debuglog_enable;

#endif
