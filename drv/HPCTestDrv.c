
/*
* Copyright University of North Carolina, 2018
* Author: Sanjeev Das (sdas@cs.unc.edu)
*
* The context switch hooking part of the code is obtained from 
* https://github.com/SouhailHammou/Drivers/blob/master/SwapContextHook/swapcontext_hook.c
*
*/

#include <ntifs.h>
#include <wdm.h>
#include <Ntstrsafe.h>


/***************Configurable parameters***********************/

//a) Choose mode either as SAMPLING_MODE or POLLING_MODE
#define SAMPLING_MODE	// SAMPLING_MODE or POLLING_MODE

#ifdef SAMPLING_MODE
	//b) set threshold value for PMI
	INT32 pmiThreshold = -50000;
#else
	//polling mode
	//b) set threshold as 0
	INT32 pmiThreshold = 0;
#endif

//c) Test process/application that has to be monitored
#define TEST_APP "test.exe"

//d) change output file path
#define LOG_FILE L"\\DosDevices\\C:\\Users\\Sanjeev\\Desktop\\hpcoutput.csv"

//Configure HPC events to monitor userspace events
#define EVENT0	0x004100C4 		//Branch instruction retired
#define EVENT1	0x004100C5  	//Mispredicted branch instructions
#define EVENT2	0x00414F2E 		//LLC cache reference
#define EVENT3	0x0041412E 		//LLC misses

//maximum number of PMI that can be recorded, depends on how much memory can be used by Win kernel driver
#define MAXVAL 1000000

//buffer size required for writing/reading into the file
#define BUFFER_SIZE 500

/************************************************************/


/* Windows OS Function Prototypes for KMDF */
NTSTATUS MyDriverUnsupportedFunction(PDEVICE_OBJECT DeviceObject, PIRP Irp);
DRIVER_UNLOAD MyDriverUnload;
VOID MyDriverUnload(PDRIVER_OBJECT  DriverObject);
NTSTATUS DriverEntry(PDRIVER_OBJECT  pDriverObject, PUNICODE_STRING  pRegistryPath);
NTKERNELAPI void KiDispatchInterrupt(void);


typedef unsigned short	WORD;
typedef unsigned char	BYTE;
typedef unsigned long	ULONG;

#define SIOCTL_TYPE 40000
#define IOCTL_HELLO\
 CTL_CODE( SIOCTL_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_DATA|FILE_WRITE_DATA)

/* Compile directives. */
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MyDriverUnload)
#pragma alloc_text(PAGE, MyDriverUnsupportedFunction)

#pragma pack(1)
typedef struct _DESC {
  UINT16 offset00;
  UINT16 segsel;
  CHAR unused:5;
  CHAR zeros:3;
  CHAR type:5;
  CHAR DPL:2;
  CHAR P:1;
  UINT16 offset16;
} DESC, *PDESC;
#pragma pack()

#pragma pack(1)
typedef struct _IDTR {
  UINT16 bytes;
  UINT32 addr;
} IDTR;
#pragma pack()

PIO_STACK_LOCATION irpSp;

/* Global variable for storing old ISR address. */
UINT32 oldISRAddressPmi = NULL;
UINT32 oldISRAddressTrap = NULL;



//some flags used for logic
int IsHpcStoredAtContextSwitch = 0;
int IsTestAppRunning = 0; 
int IsCurrentProcessTestApp = 0; 	

int trapCount = 0;					// counts number of "int 2e" in source code 
int perfCounterId = 0; 				// identifies the counter of the 7 HPCs.
int hpcCount = 0; 					// no. of times record were taken

//64 bit is required for recording counter values: ecx.eax
UINT64 hpcData[7][MAXVAL+1];

//Used to store/restore values at context switch
UINT32  counter0LowVal = 0, counter0HighVal = 0, counter1LowVal = 0, counter1HighVal = 0, \
counter2LowVal = 0, counter2HighVal = 0, counter3LowVal = 0, counter3HighVal = 0, \
counter4LowVal = 0, counter4HighVal = 0, counter5LowVal = 0, counter5HighVal = 0, \
counter6LowVal = 0, counter6HighVal = 0;
 

void InitializeCounters();
void WriteMSR(int lowVal, int highVal, int addr);
INT64 ReadMSR(int addr);  
void RecordHPC(int addr);
void RecordFinalSample(int lowVal, int highVal);

/*
*	log HPC counter values in an output file;
*/
int LogHPCData(){
	UNICODE_STRING uniName;
	OBJECT_ATTRIBUTES objAttr;
	HANDLE handle;
	NTSTATUS ntStatus;
	IO_STATUS_BLOCK ioStatusBlock;
	CHAR buffer[BUFFER_SIZE];
	size_t cb;
	int i = 0;

	RtlInitUnicodeString(&uniName, LOG_FILE);
	InitializeObjectAttributes(&objAttr, &uniName,OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	// Do not try to perform any file operations at higher IRQL levels.
	if(KeGetCurrentIrql() != PASSIVE_LEVEL)
		return STATUS_INVALID_DEVICE_STATE;

	//creates an output file
	ntStatus = ZwCreateFile(&handle,GENERIC_WRITE,&objAttr, &ioStatusBlock, NULL,FILE_ATTRIBUTE_NORMAL, 0,FILE_OVERWRITE_IF,FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	//write recorded HPC values into an output file
	if(NT_SUCCESS(ntStatus)){
		ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer),"ins,l_cycle,ref_cycle,event1,event2,event3,event4\r\n");
		if(NT_SUCCESS(ntStatus)) {
			ntStatus = RtlStringCbLengthA(buffer, sizeof(buffer), &cb);
			if(NT_SUCCESS(ntStatus)) {
				ntStatus = ZwWriteFile(handle, NULL, NULL, NULL, &ioStatusBlock, buffer, cb, NULL, NULL);
			}
		}
		for(i=0; i<hpcCount; i++){
			ntStatus = RtlStringCbPrintfA(buffer, sizeof(buffer),"%llu,%llu,%llu,%llu,%llu,%llu,%llu\r\n", hpcData[0][i],hpcData[1][i],hpcData[2][i],hpcData[3][i],hpcData[4][i],hpcData[5][i],hpcData[6][i]);
			if(NT_SUCCESS(ntStatus)) {
				ntStatus = RtlStringCbLengthA(buffer, sizeof(buffer), &cb);
				if(NT_SUCCESS(ntStatus)) {
					ntStatus = ZwWriteFile(handle, NULL, NULL, NULL, &ioStatusBlock, buffer, cb, NULL, NULL);
				}
			}
		}
		ZwClose(handle);
	}
	return 1;
 }

/*
 * Get the address of IDT table.
 */
IDTR GetIDTAddress() {
    IDTR idtrAddr;
  /* get address of the IDT table */
    __asm {
        cli;
		sidt idtrAddr;
		sti;
    }
  DbgPrint("Address of IDT table is: %x.\r\n", idtrAddr.addr);
  return idtrAddr;
}

/*
 * Get the address of the service descriptor.
 */
PDESC GetDescriptorAddress(UINT16 service) {
  /* allocate local variables */
  IDTR idtrAddr; 
  PDESC descAddr; 

  idtrAddr = GetIDTAddress();

  /* get the address of the interrupt entry we would like to hook */
  descAddr = idtrAddr.addr + service * 0x8;
  DbgPrint("Address of IDT Entry is: %x.\r\n", descAddr);

  return descAddr;
}

/*
 * Get the ISR address.
 */
UINT32 GetISRAddress(UINT16 service) {
  PDESC descAddr; 
  UINT32 isrAddr; 

  descAddr  = GetDescriptorAddress(service);

  /* calculate address of ISR from offset00 and offset16 */
  isrAddr = descAddr->offset16;
  isrAddr = isrAddr << 16;
  isrAddr += descAddr->offset00;
  DbgPrint("Address of the ISR is: %x.\r\n", isrAddr);

  /* store old ISR address in global variable, so we can use it later */
  if (service == 0xfe)
	oldISRAddressPmi = isrAddr;
  else
	oldISRAddressTrap = isrAddr;

  return isrAddr;
}

/*
 * Records HPC data
 */
void RecordHPCSample(INT64 combinedVal) {
	
	if(perfCounterId == 0){
		#ifdef SAMPLING_MODE
			hpcData[perfCounterId][hpcCount] = abs((INT32)pmiThreshold-combinedVal);
		#else
			hpcData[perfCounterId][hpcCount] = combinedVal;
		#endif
	}else{
		hpcData[perfCounterId][hpcCount] = combinedVal;	
	}
	perfCounterId++;
}

/*
 * Hook function for software interrupt only
 */
__declspec(naked) HookTrap() {
	__asm {
	//save the context of hardware interrupt
		pushfd
		pushad
		push fs
		push ds
		push es
	}

	trapCount++;
	if (trapCount == 2){
		perfCounterId = 0;
		RecordHPC(0x309);
		RecordHPC(0x30A);
		RecordHPC(0x30B);
		RecordHPC(0xC1);
		RecordHPC(0xC2);
		RecordHPC(0xC3);
		RecordHPC(0xC4);
		hpcCount++;
	}

	//zero out counters
	WriteMSR(0x00000000, 0x00000000, 0x309);
	WriteMSR(0x00000000, 0x00000000, 0x30A);
	WriteMSR(0x00000000, 0x00000000, 0x30B);
	WriteMSR(0x00000000, 0x00000000, 0xC1);
	WriteMSR(0x00000000, 0x00000000, 0xC2);
	WriteMSR(0x00000000, 0x00000000, 0xC3);
	WriteMSR(0x00000000, 0x00000000, 0xC4);

	__asm{
	//Retrieve the context of hardware interrupt
		pop es
		pop ds
		pop fs
		popad
		popfd
		jmp oldISRAddressTrap
	}
}

/*
 * Record HPC values at PMI
 */
__declspec(naked) HookPMI() {
	__asm {
	//save the context of hardware interrupt
		pushfd
		pushad
		push fs
		push ds
		push es
	}

	if(IsCurrentProcessTestApp==1 && hpcCount<=MAXVAL){
		perfCounterId = 0;
		RecordHPC(0x309);
		RecordHPC(0x30A);
		RecordHPC(0x30B);
		RecordHPC(0xC1);
		RecordHPC(0xC2);
		RecordHPC(0xC3);
		RecordHPC(0xC4);
		hpcCount++;
	}

	//set threshold for fixed_ctr0
	WriteMSR(pmiThreshold, 0x0000FFFF, 0x309);

	//Zero out remaining counters
	WriteMSR(0x00000000, 0x00000000, 0x30A);
	WriteMSR(0x00000000, 0x00000000, 0x30B);
	WriteMSR(0x00000000, 0x00000000, 0xC1);
	WriteMSR(0x00000000, 0x00000000, 0xC2);
	WriteMSR(0x00000000, 0x00000000, 0xC3);
	WriteMSR(0x00000000, 0x00000000, 0xC4);

	//Clear the overflow flag via IA32_PERF_GLOBAL_OVF_CTRL MSR
	WriteMSR(0x00000000, 0x00000001, 0x390);

	__asm{
		//Retrieve the context of hardware interrupt
		pop es
		pop ds
		pop fs
		popad
		popfd

		//jump to original OS handler
		jmp oldISRAddressPmi
	}
}

/*
 * Hook the interrupt descriptor by overwriting its ISR pointer.
 */
void HookISR(UINT16 service, UINT32 hookaddr) {
  UINT32 isrAddr; 
  UINT16 hookAddrLow;
  UINT16 hookAddrHigh;
  PDESC descAddr;

  /* check if the ISR was already hooked */
  isrAddr = GetISRAddress(service);
  if(isrAddr == hookaddr) {
    DbgPrint("The service %x already hooked.\r\n", service);
  } else {
    DbgPrint("Hooking interrupt %x: ISR %x --> %x.\r\n", service, isrAddr, hookaddr);
    descAddr  = GetDescriptorAddress(service);
    DbgPrint("Hook Address: %x\r\n", hookaddr);
    hookAddrLow = (UINT16)hookaddr;
    hookaddr = hookaddr >> 16;
    hookAddrHigh = (UINT16)hookaddr;
    DbgPrint("descAddr: %x\r\n", descAddr->offset00);
    DbgPrint("descAddr: %x\r\n", descAddr->offset16);

    __asm { cli }
    descAddr->offset00 = hookAddrLow;
    descAddr->offset16 = hookAddrHigh;
    __asm { sti }
  }
}

/*
	Find a relevant process and re/store performance counter values
*/
void SaveRestoreCounters(){
	PUCHAR pKTHREADCurr, pKTHREADNext;
	PUCHAR ProcessCurr, ProcessNext;
	PUCHAR ImageFileNameCurr, ImageFileNameNext;

	//check the exiting process and store HPC values if it is our test process
	//edi: points to the exiting thread
	//esi: points to the incoming thread

	__asm{
		// obtain exiting process
		mov pKTHREADCurr, edi
		mov ecx, CR3;
	}
	ProcessCurr = *(PUCHAR*)(pKTHREADCurr + 0x50);
	ImageFileNameCurr = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,16,'Hbf1');
	if(ImageFileNameCurr != NULL){
		/*Copy the image name to an allocated space*/
		strncpy((char*)ImageFileNameCurr,(char*)(ProcessCurr+0x16c), 16);

		//If the exiting process is a test process, we store the performance counter values
		if(!strcmp((char*)ImageFileNameCurr, TEST_APP)){

			__asm{
			//Store HPC values into memory
				mov ecx, 0x309
				rdmsr
				mov counter0LowVal, eax
				mov counter0HighVal, edx

				mov ecx, 0x30A
				rdmsr
				mov counter1LowVal, eax
				mov counter1HighVal, edx

				mov ecx, 0x30B
				rdmsr
				mov counter2LowVal, eax
				mov counter2HighVal, edx

				mov ecx, 0xC1
				rdmsr
				mov counter3LowVal, eax
				mov counter3HighVal, edx

				mov ecx, 0xC2
				rdmsr
				mov counter4LowVal, eax
				mov counter4HighVal, edx

				mov ecx, 0xC3
				rdmsr
				mov counter5LowVal, eax
				mov counter5HighVal, edx

				mov ecx, 0xC4
				rdmsr
				mov counter6LowVal, eax
				mov counter6HighVal, edx

				mov ecx, 1
				mov IsHpcStoredAtContextSwitch, ecx

			}
		}
	}else{
			goto TestRegistrycleanup;
	}

	//check the incoming process and restore HPC values if the process is our test process
	__asm{
		mov pKTHREADNext, esi
	}
	ProcessNext = *(PUCHAR*)(pKTHREADNext + 0x50);
	ImageFileNameNext = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,16, 'Hbf2');
	if(ImageFileNameNext != NULL){
		strncpy((char*)ImageFileNameNext,(char*)(ProcessNext+0x16c), 16);

		if(!strcmp((char*)ImageFileNameNext,TEST_APP)){
			if(IsTestAppRunning==0){
				//If the test process is going to run for the first time, start HPC monitoring
				IsTestAppRunning=1;
				InitializeCounters();
			}
			IsCurrentProcessTestApp = 1;	//indicates that the current process is a test process

			if (IsHpcStoredAtContextSwitch==1){
				IsHpcStoredAtContextSwitch=0;
				WriteMSR(counter0LowVal, counter0HighVal, 0x309);
				WriteMSR(counter1LowVal, counter1HighVal, 0x30A);
				WriteMSR(counter2LowVal, counter2HighVal, 0x30B);
				WriteMSR(counter3LowVal, counter3HighVal, 0xC1);
				WriteMSR(counter4LowVal, counter4HighVal, 0xC2);
				WriteMSR(counter5LowVal, counter5HighVal, 0xC3);
				WriteMSR(counter6LowVal, counter6HighVal, 0xC4);
			}

		}
		else
			IsCurrentProcessTestApp = 0;
	}
	else{
			goto TestRegistrycleanup;
	}

	//free runtime memory
	TestRegistrycleanup:
		if(ImageFileNameCurr != NULL)
			ExFreePoolWithTag(ImageFileNameCurr, 'Hbf1');
		if(ImageFileNameNext != NULL)
			ExFreePoolWithTag(ImageFileNameNext, 'Hbf2');

}

/*
	Re/store performance counter values at the context switches
*/
//__fastcall SwapContext(PKTHREAD CurrentThread,PKTHREAD NextThread)
__declspec(naked) void HooKCS(){
	SaveRestoreCounters();
	/*before jumping back execute the overwritten functions*/
	//807e3900        cmp     byte ptr [esi+39h],0
	//7404            je      nt!SwapContext+0xa (828bdaea)
	__asm{
			cmp byte ptr[esi+39h],0
			//je address , replaced in runtime
			_emit 0x0F
			_emit 0x84
		
			_emit 0xAA
			_emit 0xAA
			_emit 0xAA
			_emit 0xAA
			//jmp just after the patched bytes
			_emit 0xE9
			
			_emit 0xBB
			_emit 0xBB
			_emit 0xBB
			_emit 0xBB
	}
}

/*
	Read the leftover counter values of a process that were stored during context switch for the last PMI window
*/
void ReadFinalSample(){
 	__asm {

	//checkmaxval1:
	//don't exceed the maximum times that we can monitor; only required for offline analysis; limitation comes from how much data can a kernel driver hold
		mov ecx, hpcCount
		cmp ecx, MAXVAL
		jae terminateRead

	//restore values only when store has been done at context switch
		mov ecx, IsHpcStoredAtContextSwitch
		cmp ecx, 1
		jne terminateRead

	//readleftover:
		mov ecx, 0
		mov IsHpcStoredAtContextSwitch, ecx

		mov DWORD PTR [perfCounterId], 0

		mov eax, counter0LowVal
		mov edx, edx
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter1LowVal
		mov edx, counter1HighVal
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter2LowVal
		mov edx, counter2HighVal
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter3LowVal
		mov edx, counter3HighVal
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter4LowVal
		mov edx, counter4HighVal
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter5LowVal
		mov edx, counter5HighVal
		push edx
		push eax
		call RecordFinalSample

		mov eax, counter6LowVal
		mov edx, counter6HighVal
		push edx
		push eax
		call RecordFinalSample

		inc DWORD PTR [hpcCount] 	//counts the no. of times reading were taken

	terminateRead:

	}
}

/*
* Write into MSR registers
*/
void WriteMSR(int lowVal, int highVal, int addr){
	__asm{
		mov eax, lowVal
		mov edx, highVal
		mov ecx, addr
		wrmsr
	}
}

/*
* Record HPC value, and store the HPC count into array
*/
void RecordHPC(int addr){
	INT64 combinedVal = 0;
	combinedVal = ReadMSR(addr);
	RecordHPCSample(combinedVal);
}

/*
* Extract 48-bit counter value
*/
INT64 Extract48BitVal(int lowVal, int highVal){
	INT64 combinedHPCVal = 0;
	combinedHPCVal = (0x0000ffff & highVal);
	combinedHPCVal <<= 32;
	combinedHPCVal = combinedHPCVal + lowVal;
	return combinedHPCVal;
}

/*
* Store HPC values for final sample into array
*/
void RecordFinalSample(int lowVal, int highVal){
	INT64 combinedVal = 0;
	combinedVal = Extract48BitVal(lowVal, highVal);
	RecordHPCSample(combinedVal);
}
	
/*
* Read MSR registers
*/
INT64 ReadMSR(int addr){
	int lowVal = 0, highVal = 0; 
	INT64 combinedVal = 0;
	
	__asm{
		mov ecx, addr
		rdmsr
		mov lowVal, eax
		mov highVal, edx
	}
	combinedVal = Extract48BitVal(lowVal, highVal);
	return combinedVal;
}

/*
* initializatizing HPCs
*/
void InitializeCounters(){
	#ifdef SAMPLING_MODE	
		WriteMSR(0x0000022A, 0x00000000, 0x38D);
		WriteMSR(pmiThreshold, 0x0000FFFF, 0x309);
	#else			// polling_mode
		WriteMSR(0x00000222, 0x00000000, 0x38D);
		WriteMSR(0x00000000, 0x00000000, 0x309);
	#endif
	
	//Configure programmable counters for different events
	WriteMSR(EVENT0, 0x00000000, 0x186);
	WriteMSR(EVENT1, 0x00000000, 0x187);
	WriteMSR(EVENT2, 0x00000000, 0x188);
	WriteMSR(EVENT3, 0x00000000, 0x189);

	//Zero out remaining counters
	WriteMSR(0x00000000, 0x00000000, 0x30A);
	WriteMSR(0x00000000, 0x00000000, 0x30B);

	WriteMSR(0x00000000, 0x00000000, 0xC1);
	WriteMSR(0x00000000, 0x00000000, 0xC2);
	WriteMSR(0x00000000, 0x00000000, 0xC3);
	WriteMSR(0x00000000, 0x00000000, 0xC4);

	WriteMSR(0x0000000F, 0x00000007, 0x38F); //Enable counter globally - IA32_PERF_GLOBAL_CTRL MSR

}

/*
 * DriverEntry: entry point for drivers.
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT  pDriverObject, PUNICODE_STRING  pRegistryPath) {
    NTSTATUS NtStatus = STATUS_SUCCESS;
    unsigned int uiIndex = 0;
    PDEVICE_OBJECT pDeviceObject = NULL;
    UNICODE_STRING usDriverName, usDosDeviceName;

	//-----------
	char detourBytes[] = {0xe9,0xaa,0xbb,0xcc,0xdd,0x90};	//jmp loc_ddccbbaa; nop
	unsigned int savedCR0;
	KIRQL Irql;
	int i;

	/*KiDispatchInterrupt is exported*/
	//obtain address of SwapContext using KiDispatchInterrupt
	
	PUCHAR p = (PUCHAR)KiDispatchInterrupt;
	unsigned int relative = *(unsigned int*)(p + 0xDE);   
	PUCHAR SwapContext = (PUCHAR)((unsigned int)(p + 0xDD) + relative + 5); //pointer -> SwapContext
	PUCHAR det =  (PUCHAR)HooKCS; //detour to -> HookCS
	//-----------
    
	DbgPrint("DriverEntry Called \r\n");
    RtlInitUnicodeString(&usDriverName, L"\\Device\\MyDriver");
    RtlInitUnicodeString(&usDosDeviceName, L"\\DosDevices\\MyDriver");

    NtStatus = IoCreateDevice(pDriverObject, 0, &usDriverName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &pDeviceObject);

    if(NtStatus == STATUS_SUCCESS) {
        /* MajorFunction: is a list of function pointers for entry points into the driver. */
        for(uiIndex = 0; uiIndex < IRP_MJ_MAXIMUM_FUNCTION; uiIndex++)
             pDriverObject->MajorFunction[uiIndex] = MyDriverUnsupportedFunction;

		 /* DriverUnload is required to be able to dynamically unload the driver. */
		pDriverObject->DriverUnload =  MyDriverUnload;
		pDeviceObject->Flags |= 0;
		pDeviceObject->Flags &= (~DO_DEVICE_INITIALIZING);

		/* Create a Symbolic Link to the device. MyDriver -> \Device\MyDriver */
        IoCreateSymbolicLink(&usDosDeviceName, &usDriverName);
	}

	//------------Hook context switching-------------
	DbgPrint("KiSwapContext at : %p\n",SwapContext);
	
	/*Implement the inline hook*/
	relative = (unsigned int)HooKCS - (unsigned int)SwapContext - 5; 	//offset of HooKCS relative to SwapContext

	*(unsigned int*)&detourBytes[1] = relative; //set loc_ddccbbaf = offset -> HooKCS
	
	/*Disable write protection*/
	__asm{
		push eax
		mov eax,CR0
		mov savedCR0,eax
		and eax,0xFFFEFFFF
		mov CR0,eax
		pop eax
	}
	
	/*Set the detour function jump addresses in runtime*/
	for(i=0;;i++){
		if(det[i] == 0xAA && det[i+1] == 0xAA && det[i+2] == 0xAA && det[i+3] == 0xAA)
			break;
	}
	/*set the relative address for the conditional jump ()*/
	//je      nt!SwapContext+0xa
	*(unsigned int*)&det[i] = (unsigned int)((SwapContext+0xa) - (det+i-2) - 6);
	 
	
	/*set the relative address for the jump back to SwapContext*/
	for(;;i++){
		if(det[i] == 0xBB && det[i+1] == 0xBB && det[i+2] == 0xBB && det[i+3] == 0xBB)
			break;
	}
	//jmp SwapContext+6
	*(unsigned int*)&det[i] = (unsigned int)((SwapContext + 6) - (det+i-1) - 5);
	
	/*Raise IRQL to patch safely*/
	Irql = KeRaiseIrqlToDpcLevel();
	
	/*implement the patch*/
	for(i=0;i<6;i++){
		SwapContext[i] = detourBytes[i]; 	//jmp loc_ddccbbaf; nop
	}
	KeLowerIrql(Irql);
	
	/*restore the write protection*/
	__asm{
		push eax
		mov eax,savedCR0
		mov CR0,eax
		pop eax
	}
	//------------------------------------

	#ifdef SAMPLING_MODE
		
		// We hook the default PMI handling by OS using interrupt descriptor table. "0xFE" is vector for PMI.
		HookISR(0xfe, (UINT32)HookPMI);
	#else
		//Hook the software interrupt
		HookISR(0x2e, (UINT32)HookTrap);	//Also tested with "0x03" interrupt
	#endif

	return NtStatus;
}

/*
  * MyDriverUnload: called when the driver is unloaded.
  */
VOID MyDriverUnload(PDRIVER_OBJECT  DriverObject) {
	UNICODE_STRING usDosDeviceName;
	NTSTATUS NtStatus = STATUS_SUCCESS;
	int i=0;

	//---------For Hooking Context Switch---
	unsigned int savedCR0;
	KIRQL Irql;
	PUCHAR p = (PUCHAR)KiDispatchInterrupt;
	unsigned int relative = *(unsigned int*)(p + 0xDE);
	PUCHAR KiSwapContext = (PUCHAR)((unsigned int)(p+0xDD) + relative + 5);
	//807e3900        cmp     byte ptr [esi+39h],0
	//7404            je      nt!SwapContext+0xa (828bdaea)
	char savedOps[] = {0x80,0x7e,0x39,0x00,0x74,0x04};		//cmp byte ptr [esi+0x39], 0; je loc_0000000a
	//------------------------------

	//log the leftover counter values of a process that were stored during context switch for the last PMI window
	#ifdef SAMPLING_MODE
		ReadFinalSample();
	#endif


	#ifdef SAMPLING_MODE
		if(oldISRAddressPmi != NULL) {
			HookISR(0xfe, (UINT32)oldISRAddressPmi);
		}
	#else
		if(oldISRAddressTrap != NULL) {
			HookISR(0x2e, (UINT32)oldISRAddressTrap);	//also tested with other interrupts such as "0x03"
		}
	#endif
	//------------------------------
	
	/*Restore write protection*/
	__asm{
		push eax
		mov eax,CR0
		mov savedCR0,eax
		and eax,0xFFFEFFFF
		mov CR0,eax
		pop eax
	}
	Irql = KeRaiseIrqlToDpcLevel();
	for(i=0;i<6;i++){
		KiSwapContext[i] = savedOps[i];
	}
	KeLowerIrql(Irql);
	__asm{
		push eax
		mov eax,savedCR0
		mov CR0,eax
		pop eax
	}
	//------------Un-hooking context switch ends-------------

	/* delete the driver */
    RtlInitUnicodeString(&usDosDeviceName, L"\\DosDevices\\MyDriver");
    IoDeleteSymbolicLink(&usDosDeviceName);
    IoDeleteDevice(DriverObject->DeviceObject);

	//logs HPC data into output file
	LogHPCData();

}

/*
 * MyDriverUnsupportedFunction: called when a major function is issued that isn't supported.
 */
NTSTATUS MyDriverUnsupportedFunction(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    NTSTATUS NtStatus = STATUS_NOT_SUPPORTED;
	DbgPrint("MyDriverUnsupportedFunction Called \r\n");
    return NtStatus;
}
