/******************************************************************************
* Copyright (c) 2022 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
*
* @file xplmi_plm_update.c
* This is the file which contains PLM update process.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date       Changes
* ----- ---- ---------- -------------------------------------------------------
* 1.00  bm   01/30/2022 Initial release
*
* </pre>
*
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xplmi_update.h"
#include "xplmi_wdt.h"
#include "xplmi_err.h"
#include "xplmi_modules.h"
#include "xplmi_hw.h"
#include "xplmi_ipi.h"
#include "xplmi.h"
#include "xil_util.h"

/************************** Constant Definitions *****************************/
#define XPLMI_RESET_VECTOR		(0xF0200000U)
#define XPLMI_PLM_UPDATE_PMCRAM_LEN	(0x4000U)
#define XPLMI_PLM_UPDATE_DS_START_ADDR	(XPLMI_PMCRAM_BASEADDR + 0x10000U)
#define XPLMI_PLM_UPDATE_DS_ENDADDR	(XPLMI_PLM_UPDATE_DS_START_ADDR + \
						XPLMI_PLM_UPDATE_PMCRAM_LEN - 1U)
#define XPLMI_ROM_PLM_UPDATE_REQ	(0x08U)
#define PMX_PLM_UPDATE_REASON_MASK	(0x00000008U)
#define XPLMI_ROM_INT_REASON_CLEAR	(0x0000000FU)
#define PLM_UPDATE_DONE_POLL_TIMEOUT	(1000U)
#define XPLMI_UPDATE_DB_VERSION		(1U)
#define XPLMI_DS_HDR_SIZE		(sizeof(XPlmi_DsHdr))
#define XPLMI_DS_CNT			(u32)(__data_struct_end - __data_struct_start)
#define XPLMI_UPDATE_IPIMASK_VER 	(1U)
#define XPLMI_UPDATE_IPIMASK_LCVER 	(1U)

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
static int XPlmi_PlmUpdateMgr(void) __attribute__((section(".update_mgr_a")));
static int XPlmi_CompatibilityCheck(void);

/************************** Variable Definitions *****************************/
extern XPlmi_DsEntry __data_struct_start[];
extern XPlmi_DsEntry __data_struct_end[];
extern u32 __update_mgr_b_start[];
extern u8 __update_mgr_a_fn_start[];
extern u8 __update_mgr_a_fn_end[];
static u8 PlmUpdateStatus;
static u32 PlmUpdateIpiMask __attribute__ ((aligned(4U)));
EXPORT_GENERIC_DS(PlmUpdateIpiMask, XPLMI_UPDATE_IPIMASK_DS_ID,
	XPLMI_UPDATE_IPIMASK_VER, XPLMI_UPDATE_IPIMASK_LCVER,
	sizeof(PlmUpdateIpiMask), (u32)(UINTPTR)&PlmUpdateIpiMask);

/*****************************************************************************/

/****************************************************************************/
/**
* @brief	This function will initialize In-Place Update related logic
*
* @return	XST_SUCCESS on success and error code on failure
*
****************************************************************************/
int XPlmi_UpdateInit(void)
{
	volatile int Status = XST_FAILURE;
	volatile int SStatus = XST_FAILURE;
	u32 ResponseBuffer[XPLMI_CMD_RESP_SIZE];

	PlmUpdateStatus = (u8)(((XPlmi_In32(PMC_GLOBAL_ROM_INT_REASON) &
			PMX_PLM_UPDATE_REASON_MASK) ==
			PMX_PLM_UPDATE_REASON_MASK) ? (u8)TRUE : (u8)FALSE);

	if (PlmUpdateStatus == (u8)TRUE) {
		Status = XPlmi_RestoreDataBackup();
		if (XPlmi_RomSwdtUsage() == (u8)TRUE) {
			XPlmi_KickWdt(XPLMI_WDT_INTERNAL);
		}
#ifdef XPAR_XIPIPSU_0_DEVICE_ID
		if (PlmUpdateIpiMask != 0U) {
			SStatus = XPlmi_IpiDrvInit();
			if (SStatus != XST_SUCCESS) {
				Status = XPLMI_ERR_IPI_DRV_INIT;
				goto END;
			}
			/* Fill IPI response */
			SStatus = Xil_SMemSet(ResponseBuffer, sizeof(ResponseBuffer),
					0U, sizeof(ResponseBuffer));
			if (SStatus != XST_SUCCESS) {
				Status = XPLMI_ERR_MEMSET_UPDATE_RESP;
				goto END;
			}
			ResponseBuffer[0U] = (u32)Status;
			(void)XPlmi_IpiWrite(PlmUpdateIpiMask, ResponseBuffer,
				XPLMI_CMD_RESP_SIZE, XIPIPSU_BUF_TYPE_RESP);
			/* Ack the IPI */
			XPlmi_Out32(IPI_PMC_ISR, PlmUpdateIpiMask);
		}
#else
		SStatus = XST_SUCCESS;
#endif
	}
	else {
		Status = XST_SUCCESS;
		SStatus = XST_SUCCESS;
	}

END:
	if (Status == XST_SUCCESS) {
		Status = SStatus;
	}
	return Status;
}

/****************************************************************************/
/**
* @brief	This function will Initiate the PLM Update process with ROM.
*
* @return	XST_SUCCESS on success and error code on failure
*
* @Note		No Functions should be called inside this function as this code
*		will be relocated during run time and the relative jumps used for
*		the function calls can not be resolved. However, inline functions
*		can be used provided they are forced to be inline using compiler
*		attributes.
*
****************************************************************************/
static int XPlmi_PlmUpdateMgr(void)
{
	int Status = XST_FAILURE;
	u32 RegVal;
	u32 PmcBootErr;
	u32 RomIntReason;
	u32 Index;
	void (*XPlmi_ResetVector)(void) = (void (*)(void))XPLMI_RESET_VECTOR;

	/* Send PLM update request to ROM */
	XPlmi_Out32(PMC_GLOBAL_ROM_INT, XPLMI_ROM_PLM_UPDATE_REQ);

	/* Go to Sleep */
	mb_sleep();

	/* Poll for Update Done Bit as ROM is waking up PPU1 before DONE */
	for (Index = 0U; Index < PLM_UPDATE_DONE_POLL_TIMEOUT; Index++) {
		RomIntReason = XPlmi_In32(PMC_GLOBAL_ROM_INT_REASON);
		if ((RomIntReason & PMX_PLM_UPDATE_REASON_MASK)
				== PMX_PLM_UPDATE_REASON_MASK) {
			break;
		}
	}
	/* Check for the Boot Errors */
	PmcBootErr = XPlmi_In32(PMC_GLOBAL_PMC_BOOT_ERR);
	/* No Boot Errors and Plm Update is Done, execute new PLM */
	if ((PmcBootErr == 0x0U) && (Index < PLM_UPDATE_DONE_POLL_TIMEOUT)) {
		/* Jump to Reset Vector location to execute new PLM */
		XPlmi_ResetVector();
		/* End */
	} else {
#ifndef PLM_DEBUG_MODE
		/* Boot Errors, perform IPOR */
		RegVal = XPlmi_In32(CRP_RST_PS);
		XPlmi_Out32(CRP_RST_PS, RegVal | CRP_RST_PS_PMC_POR_MASK);
		if ((XPlmi_In32(CRP_BOOT_MODE_USER) &
			CRP_BOOT_MODE_USER_BOOT_MODE_MASK) == 0U)
#endif
		{
			while(TRUE);
		}
	}

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function checks if Inplace PLM update occurs or not.
 *
 * @return	TRUE if Inplace PLM Update and FALSE otherwise
 *
 *****************************************************************************/
u8 XPlmi_IsPlmUpdateDone(void)
{
	return PlmUpdateStatus;
}

/*****************************************************************************/
/**
 * @brief	This function checks compatibility between data structures of
 * 		old and new PLM
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_CompatibilityCheck(void)
{
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 * @brief	This function does operations like storing, restoring the Data
 * 		Structure to Memory during PLM update
 *
 * @param	Op is the type of operation to be performed on the data structure
 * @param	Addr is the memory address to which data structure should be stored
 * 		or restored from
 * @param 	Data is the Data Structure Entry
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_DsOps(u32 Op, u64 Addr, void *Data)
{
	int Status = XST_FAILURE;
	u32 Len = 0U;
	XPlmi_DsHdr *RestoreDsHdr = (XPlmi_DsHdr *)(UINTPTR)Addr;
	XPlmi_DsEntry *DsEntry = (XPlmi_DsEntry *)Data;

	if (DsEntry == NULL) {
		Status = XPLMI_ERR_INVALID_DS_ENTRY;
		goto END;
	}

	if (DsEntry->DsHdr.Len % XPLMI_WORD_LEN) {
		Status = XPLMI_ERR_DS_ALIGNMENT_INCORRECT;
		goto END;
	}

	if ((Addr + XPLMI_DS_HDR_SIZE + DsEntry->DsHdr.Len) >
			XPLMI_PLM_UPDATE_DS_ENDADDR) {
		Status = XPLMI_ERR_PLM_UPDATE_DB_OVERFLOW;
		goto END;
	}

	if (Op == XPLMI_STORE_DATABASE) {
		/* Copy Data Structure to given address */
		Status = Xil_SMemCpy((void *)(UINTPTR)(Addr + XPLMI_DS_HDR_SIZE),
			DsEntry->DsHdr.Len, (void *)(UINTPTR)DsEntry->Addr,
			DsEntry->DsHdr.Len, DsEntry->DsHdr.Len);
		if (Status != XST_SUCCESS) {
			Status = XPLMI_ERR_MEMCPY_STORE_DB;
			goto END;
		}
		/* Copy header to given address */
		XPlmi_Out32(Addr, DsEntry->DsHdr.Ver.HdrVal);
		XPlmi_Out32(Addr + XPLMI_WORD_LEN, DsEntry->DsHdr.Len);
	} else if (Op == XPLMI_RESTORE_DATABASE) {
		Len = DsEntry->DsHdr.Len;
		if (DsEntry->DsHdr.Len > RestoreDsHdr->Len) {
			Len = RestoreDsHdr->Len;
			/* Zeroize additional members of the data structure */
			Status = Xil_SMemSet((void *)(UINTPTR)(DsEntry->Addr + Len),
					DsEntry->DsHdr.Len - Len, 0U,
					DsEntry->DsHdr.Len - Len);
			if (Status != XST_SUCCESS) {
				Status = XPLMI_ERR_MEMSET_RESTORE_DB;
				goto END;
			}
		}
		/* Copy Data Structure from given address */
		Status = Xil_SMemCpy((void *)(UINTPTR)DsEntry->Addr, Len,
			(void *)(UINTPTR)(Addr + XPLMI_DS_HDR_SIZE), Len, Len);
		if (Status != XST_SUCCESS) {
			Status = XPLMI_ERR_MEMCPY_RESTORE_DB;
			goto END;
		}
	}
	else {
		Status = XPLMI_ERR_PLM_UPDATE_INVALID_OP;
		goto END;
	}

	Status = XST_SUCCESS;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function returns DsEntry found after searching in the
 * 		provided DsList
 *
 * @param	DsList is the Data Structure List in which the DS is searched
 * @param	DsCnt is the number of Data Structures present in the List
 * @param	DsHdr is the Data Structure Header to search for
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
static XPlmi_DsEntry* XPlmi_GetDsEntry(XPlmi_DsEntry *DsList, u32 DsCnt,
				XPlmi_DsHdr *DsHdr)
{
	XPlmi_DsEntry *Result = NULL;
	u32 Index;

	if (DsList == NULL) {
		goto END;
	}

	for (Index = 0U; Index < DsCnt; Index++) {
		if ((DsList[Index].DsHdr.Ver.ModuleId == DsHdr->Ver.ModuleId) &&
			(DsList[Index].DsHdr.Ver.DsId == DsHdr->Ver.DsId)) {
			Result = &DsList[Index];
			break;
		}
	}

END:
	return Result;
}

/*****************************************************************************/
/**
 * @brief	This function restore all the data structures after InPlace
 *		PLM udpate.
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_RestoreDataBackup(void)
{
	int Status = XST_FAILURE;
	XPlmi_DsEntry *DsEntry = NULL;
	XPlmi_DbHdr *DbHdr = (XPlmi_DbHdr *)XPLMI_PLM_UPDATE_DS_START_ADDR;
	u64 DsAddr;
	u32 EndAddr;

	if (DbHdr->HdrVersion != XPLMI_UPDATE_DB_VERSION) {
		Status = XPLMI_ERR_DB_HDR_VERSION_MISMATCH;
		goto END;
	}

	if (DbHdr->HdrSize != sizeof(XPlmi_DbHdr)) {
		Status = XPLMI_ERR_DB_HDR_SIZE_MISMATCH;
		goto END;
	}

	DsAddr = XPLMI_PLM_UPDATE_DS_START_ADDR + DbHdr->HdrSize;
	EndAddr = DsAddr + (DbHdr->DbSize * XPLMI_WORD_LEN);

	if (EndAddr > XPLMI_PLM_UPDATE_DS_ENDADDR) {
		Status = XPLMI_ERR_DB_ENDADDR_INVALID;
		goto END;
	}

	while (DsAddr < EndAddr) {
		DsEntry = XPlmi_GetDsEntry(__data_struct_start, XPLMI_DS_CNT,
				(XPlmi_DsHdr *)(UINTPTR)DsAddr);
		if (DsEntry == NULL) {
			Status = XPLMI_ERR_PLM_UPDATE_NO_DS_FOUND;
			break;
		}
		if (DsEntry->Handler == NULL) {
			Status = XPLMI_ERR_INVALID_RESTORE_DS_HANDLER;
			break;
		}
		Status = DsEntry->Handler(XPLMI_RESTORE_DATABASE,
				DsAddr, DsEntry);
		if (Status != XST_SUCCESS) {
			break;
		}
		DsAddr += XPLMI_DS_HDR_SIZE + DsEntry->DsHdr.Len;
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function store all the data structures before InPlace
 *		PLM udpate.
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
static int XPlmi_StoreDataBackup(void)
{
	int Status = XST_FAILURE;
	XPlmi_DsEntry *DsEntry = __data_struct_start;
	XPlmi_DbHdr * volatile DbHdr = (XPlmi_DbHdr *)XPLMI_PLM_UPDATE_DS_START_ADDR;
	u64 DsAddr;
	u32 DsCnt;
	u32 Index;

	//TODO - Replace memcpy & memset with xplmi alternatives
	Status = Xil_SMemSet((void *)DbHdr, sizeof(XPlmi_DbHdr), (int)0x0U,
			sizeof(XPlmi_DbHdr));
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_MEMSET_DBHDR;
		goto END;
	}
	DsCnt = XPLMI_DS_CNT;
	DbHdr->HdrVersion = XPLMI_UPDATE_DB_VERSION;
	DbHdr->HdrSize = sizeof(XPlmi_DbHdr);
	DsAddr = XPLMI_PLM_UPDATE_DS_START_ADDR + DbHdr->HdrSize;

	for (Index = 0; Index < DsCnt; Index++) {
		if (DsEntry[Index].Handler == NULL) {
			Status = XPLMI_ERR_INVALID_STORE_DS_HANDLER;
			break;
		}
		Status = DsEntry[Index].Handler(XPLMI_STORE_DATABASE,
				DsAddr, &DsEntry[Index]);
		if (Status != XST_SUCCESS) {
			break;
		}
		DsAddr += XPLMI_DS_HDR_SIZE + DsEntry[Index].DsHdr.Len;
	}
	if (Index == DsCnt) {
		DbHdr->DbSize = (DsAddr - XPLMI_PLM_UPDATE_DS_START_ADDR -
					DbHdr->HdrSize) / XPLMI_WORD_LEN;
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function shutdown all the modules gracefully
 *
 * @param	Op is the operation structure passed to determine type of
 * 		operation and other details
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
static int XPlmi_ShutdownModules(XPlmi_ModuleOp Op)
{
	int Status = XST_FAILURE;
	int Index;

	for (Index = XPLMI_MAX_MODULES - 1; Index >= 0; --Index) {
		if (Modules[Index] == NULL) {
			continue;
		}
		if (Modules[Index]->UpdateHandler != NULL) {
			Status = Modules[Index]->UpdateHandler(Op);
			if (Status != XST_SUCCESS) {
				break;
			}
		}
	}

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function does In-Place PLM Update
 *
 * @param	Cmd is the command pointer of in place update command
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_PlmUpdate(XPlmi_Cmd *Cmd)
{
	int Status = XST_FAILURE;
	u32 PdiAddr = Cmd->Payload[0U];
	XPlmi_ModuleOp Op;
	u32 UpdMgrSize = __update_mgr_a_fn_end - __update_mgr_a_fn_start;
	int (*XPlmi_RelocatedFn)(void) =
			(int (*)(void))(UINTPTR)__update_mgr_b_start;

	XPlmi_Printf(DEBUG_GENERAL, "InPlace PLM Update started with new PLM "
			"from Pdi Address: 0x%x\n\r", PdiAddr);

	/* Check version compatibility */
	Status = XPlmi_CompatibilityCheck();
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_PLM_UPDATE_COMPATIBILITY;
		goto END;
	}

	/* Initiate Shutdown of Modules */
	Op.Mode = XPLMI_MODULE_SHUTDOWN_INITIATE;
	Status = XPlmi_ShutdownModules(Op);
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_PLM_UPDATE_SHUTDOWN_INIT;
		goto END;
	}

	/* Check and Wait till Modules Shutdown is Completed*/
	Op.Mode = XPLMI_MODULE_SHUTDOWN_COMPLETE;
	Status = XPlmi_ShutdownModules(Op);
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_PLM_UPDATE_SHUTDOWN_COMPLETE;
		goto END;
	}

	/* Store IPI Mask to ack after update */
	PlmUpdateIpiMask = Cmd->IpiMask;

	/* Data Backup */
	Status = XPlmi_StoreDataBackup();
	if (Status != XST_SUCCESS) {
		goto END;
	}

	if (XPlmi_RomSwdtUsage() == (u8)TRUE) {
		/* Kick PMC WDT before requesting update */
		XPlmi_KickWdt(XPLMI_WDT_INTERNAL);
	}
	else {
		/* Stop PMC WDT before requesting update */
		XPlmi_StopWdt(XPLMI_WDT_INTERNAL);
	}

	/* Update the new PLM location in Memory */
	XPlmi_Out32(PMC_GLOBAL_GLOBAL_GEN_STORAGE5, PdiAddr);

	/* Clear Previous Done Bits */
	XPlmi_Out32(PMC_GLOBAL_ROM_INT_REASON, XPLMI_ROM_INT_REASON_CLEAR);

	/* Relocate PLM Update Manager to reserved safe location */
	Status = Xil_SMemCpy((u8 *)(UINTPTR)__update_mgr_b_start, UpdMgrSize,
		(const void *)&XPlmi_PlmUpdateMgr, UpdMgrSize, UpdMgrSize);
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_MEMCPY_RELOCATE;
		goto END;
	}

	/* Jump to relocated PLM Update Manager */
	Status = XPlmi_RelocatedFn();
	if (Status != XST_SUCCESS) {
		Status = XPLMI_ERR_PLM_UPDATE_RELOCATED_FN;
	}

END:
	return Status;
}
