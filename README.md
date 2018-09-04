HPCTool 
==============================
Features:
------------------------------
- Monitors hardware performance counters 
- Features per-process filtering
- Features two modes:
	1. **POLLING** mode reads performance counter values at a desired state. We instrument source code using software trap (int 2e) to indicate the desired state.
	2. **SAMPLING** mode reads performance counter values at a performance monitoring interrupt (PMI), which is triggered by setting a threshold value on an  event. In our code, we set a threshold value on instruction retired event. 


Requirements: 
------------------------------
1. The HPCTool has been tested on 32 bit, Windows 7, both bare-metal and virtualized.
	- To use the HPCTool in a virtualized environment, verify that the virtual performance counters are enabled. 
		For example, in VMWare Fusion one needs to modify following settings:
		- Enable virtual CPU Performance counters (e.g., on VMware -> settings: VMware->Hardware)
		- In .vmx file add below lines (on VMware):
			- vpmc.enable = "TRUE"
			- vpmc.freezeMode="guest"
		
2. To build the kernel driver, install Windows Driver Kit version 7 available at [Microsoft](https://www.microsoft.com/en-us/download/confirmation.aspx?id=11800). You will need at least the "Full Development Environment" edition.

Building the HPCtool:
--------------------------------
1. Open [drv/HPCTestDrv.c](./drv/HPCTestDrv.c) and make following changes
	
	a. Set the mode of operation to "POLLING_MODE" OR "SAMPLING_MODE".
	
	```bash
		#define SAMPLING_MODE
	```
	b. Set threshold values for generating PMI. 
	 - In SAMPLING_MODE, set pmiThreshold as desired. 
	 - In POLLING_MODE,  set pmiThreshold = 0. 
	
	```bash
		#ifdef SAMPLING_MODE
			INT32 pmiThreshold = -50000;
		#else	//polling mode
			INT32 pmiThreshold = 0;
		#endif
	```

	c. Change TEST_APP to a process/application that has to be monitored.
	
	```bash
		#define TEST_APP "test.exe"
	```

	d. Modify LOG_FILE, to reflect your environment e.g. "C:\\Users\\Sanjeev\\Desktop\\hpcoutput.csv" 
	
	```bash
		#define LOG_FILE L"\\DosDevices\\C:\\Users\\Sanjeev\\Desktop\\hpcoutput.csv"
	```

	e. Set the event type of the performance counters EVENT0, EVENT1, EVENT2, EVENT3 to measure the events of interests. 
	 - By default only user space events are monitored for the following events: 
		- EVENT0: Number of branches retired,
		- EVENT1: Number of mis-predicted branches retired,
		- EVENT2: Number of last level cache references,
		- EVENT3: Number of last level cache misses. 
	
	```bash
		#define EVENT0	0x004100C4		//Branch instruction retired
		#define EVENT1	0x004100C5		//Mispredicted branch instructions
		#define EVENT2	0x00414F2E		//LLC cache reference
		#define EVENT3	0x0041412E		//LLC misses
	```

	 E.g., to monitor Branch Instruction retired (Event Num. = C4H, Umask Value = 00H), 
	 in user space:  we set EVENT0 = 0x4100C4
	 in kernel space: we set EVENT0 = 0x4200C4 

	 Refer to our [tutorial](./tutorial/tutorial.md) for further details on configuring each event. 
	
	 Refer to [Intel Manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf), Chapter 19 for more information event num and umask value for performance counter events.
2. Open **x86 Checked Build Environment** command prompt with **Administrator** privilege (right click -> Run as Administrator)
3. Change the current directory to the path containing the kernel driver source: 
	
	```bash
		cd PATH-TO-SRC-DIR 
		builddrv.bat
	```
	If the build is successful, HPCTestDrv.sys will be copied to Windows driver directory (e.g., C:\Windows\System32\drivers)



Preparing the instrumented binary: 
--------------------------------
1. The binary that will be measured needs to be instrumented in the source code as shown in the test example in testcode\test.c
	For C/C++ code, insert the following instrumentation trigger to generate the software trap before and after the section of code to be profiled:
	
	```bash
		__asm __volatile{ 
			mov eax, 19h 
			int 0x2e 
		}
	```
2. Compile the source code using C compiler and run HPC tool on the compiled binary.


Running the HPCTool:
--------------------------------
1. Disable the driver signing 
	In the privileged command prompt opened in step 2) type:

	```bash
		bcdedit.exe -set loadoptions DDISABLE_INTEGRITY_CHECKS
		bcdedit.exe -set TESTSIGNING ON
	```
2. Install the kernel driver - you need to do this just once
	
	Double click **InstallTestDrv.reg** and click to accept the registry of the kernel driver. **Restart** the system.

3. Create an empty file in the location specified in **LOG_FILE**

4. Open a command prompt with Administrator privileges and run the test program using the sample **runtest.bat** script. It starts the HPC driver, executes the test program and finally stops the driver. 

	```bash
		runtest.bat
	```

**Output:** HPC output is logged in the file specified as **LOG_FILE** at compile time. 


Output:
--------------------------------
The output comprises of collection of samples. 
- Each sample contains the measurement of 7 events -- 3 fixed and 4 programmable/configurable events.
- 3 Fixed events: No. of instructions retired, logical cycles, reference cycles
- 4 programmable events: No. of branches retired, mis-predicted branches retired, LLC cache references, LLC misses. 
- The four programmable events can be changed to address various profiling goals. However, changing the events measured requires re-compiling the kernel driver. 
- The data collected using the performance counters is written to a file in a comma separated value (CSV) format. The order of the fields is as follows:
	
	```bash	
		#instructions retired, #logical-cycles, #reference-cycles, #event0, #event1, #event2, #event3
	```
- In the sampling mode, a data point is generated every **pmiThreshold** instructions retired.
- In the polling mode there is only one data point collected after the second instrumentation trigger is invoked. 

Cite as:
--------------------------------
**If you use this tool, please cite as:**

*Das, S., Werner, J., Antonakakis, M., Polychronakis, M. and Monrose, F., 2019, May. SoK: The Challenges, Pitfalls, and Perils of Using Hardware Performance Counters for Security. To appear in Proceedings of the 40th IEEE Symposium on Security and Privacy (S&P).*