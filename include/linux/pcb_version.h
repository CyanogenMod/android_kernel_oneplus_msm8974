/************************************************************ 
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: - pcb_version.h
* Description: head file for pcb_version.
				
** Version: 1.0
** Date : 2013/10/15	
** Author: yuyi@Dep.Group.Module
************************************************************/
#ifndef _PCB_VERSION_H
#define _PCB_VERSION_H

enum {
	PCB_VERSION_UNKNOWN,
	HW_VERSION__10,		//729mV	
	HW_VERSION__11,		//900 mV	
	HW_VERSION__12,		//1200 mV	
	HW_VERSION__13,		//1484 mV
	HW_VERSION__20 = 20,
	HW_VERSION__21 = 21,
	HW_VERSION__22 = 22,
	HW_VERSION__23 = 23,
};
enum {
	RF_VERSION_UNKNOWN,
	RF_VERSION__11,		//WCDMA_GSM_China	
	RF_VERSION__12,		//WCDMA_GSM_LTE_Europe	
	RF_VERSION__13,		//WCDMA_GSM_LTE_America
	RF_VERSION__21,		//WCDMA_GSM_CDMA_China	
	RF_VERSION__22,		//WCDMA_GSM_Europe	
	RF_VERSION__23,		//WCDMA_GSM_America
	RF_VERSION__31,		//TD_GSM	
	RF_VERSION__32,		//TD_GSM_LTE	
	RF_VERSION__33,		//
	RF_VERSION__44 = 19,	//
	RF_VERSION__66 = 30,
	RF_VERSION__67,
	RF_VERSION__76,
	RF_VERSION__77,
	RF_VERSION__87,
	RF_VERSION__88,
	RF_VERSION__89,
	RF_VERSION__98,
	RF_VERSION__99,
};

int get_pcb_version(void);
int get_rf_version(void);

#endif /* _PCB_VERSION_H */
