#include <stdio.h>
#include <windows.h>


void main()
{
	//Instrument to generate trap
	__asm __volatile {
		mov eax, 19h 
		int 0x2e
	}

	printf("Hello World!\n");
	printf("Good bye cruel world.\n");
	
	//Instrument to generate trap
	__asm __volatile{ 
		mov eax, 19h; 
		int 0x2e 
	}
}	

