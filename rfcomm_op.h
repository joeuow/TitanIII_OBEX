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
* File Name:			rfcomm_op.h
* Last Modified:
* Changes:
**********************************************************************/
#ifndef __RFCOMM_OP_H
#define	__RFCOMM_OP_H

extern int SearchBTwithSerial(const char *addr, int *res_channel);
extern int RfcommConnect(const char *rbt_addr, int channel);
extern void RfcommDisconnect(int sr_fd);

#endif