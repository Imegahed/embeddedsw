/******************************************************************************
* Copyright (C) 2010 - 2021 Xilinx, Inc.  All rights reserved.
* Copyright (C) 2022 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
 * @file xiicps_selftest_example.c
 *
 * This example runs on zynqmp / versal evaluation boards using the IIC hardware
 * device and XIicPs driver.
 *
 *
 * <pre> MODIFICATION HISTORY:
 *
 * Ver	 Who Date    	Changes
 * ----- --- -------- 	-----------------------------------------------
 * 1.00a sdm 05/30/11 	First release
 * 3.13  rna 02/05/21   Changed XIicPs global variable name
 *
 * </pre>
 *
 ****************************************************************************/

/***************************** Include Files **********************************/
#include "xparameters.h"
#include "xiicps.h"
#include "xil_printf.h"

/************************** Constant Definitions ******************************/

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#define IIC_DEVICE_ID		XPAR_XIICPS_0_DEVICE_ID


/**************************** Type Definitions ********************************/

/************************** Function Prototypes *******************************/

int IicPsSelfTestExample(u16 DeviceId);

/************************** Variable Definitions ******************************/

XIicPs IicPsInstance;			/* Instance of the IIC Device */

/******************************************************************************/
/**
*
* Main function to call the Self Test example.
*
*
* @return	XST_SUCCESS if successful, XST_FAILURE if unsuccessful.
*
* @note		None.
*
*******************************************************************************/
#ifndef TESTAPP_GEN
int main(void)
{
	int Status;

	xil_printf("IIC Self Test Example \r\n");

	/*
	 * Run the Iic Self Test example, specify the Device ID that is
	 * generated in xparameters.h
	 */
	Status = IicPsSelfTestExample(IIC_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		xil_printf("IIC Self Test Failed\r\n");
		return XST_FAILURE;
	}

	xil_printf("Successfully ran IIC Self Test Example Test\r\n");
	return XST_SUCCESS;
}
#endif

/*****************************************************************************/
/**
*
* This function does a minimal test on the Iic device and driver as a
* design example. The purpose of this function is to illustrate
* how to use the XIicPs component.
*
*
* @param	DeviceId is the Device ID of the IicPs Device and is the
*		XPAR_<IICPS_instance>_DEVICE_ID value from xparameters.h
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
*
*******************************************************************************/
int IicPsSelfTestExample(u16 DeviceId)
{
	int Status;
	XIicPs_Config *Config;

	/*
	 * Initialize the IIC driver so that it's ready to use
	 * Look up the configuration in the config table, then initialize it.
	 */
	Config = XIicPs_LookupConfig(DeviceId);
	if (NULL == Config) {
		return XST_FAILURE;
	}

	Status = XIicPs_CfgInitialize(&IicPsInstance, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Perform a self-test.
	 */
	Status = XIicPs_SelfTest(&IicPsInstance);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
