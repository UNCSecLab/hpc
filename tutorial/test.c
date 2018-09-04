//--------Matrix multiplication--------
#include<stdio.h>
#include <windows.h>

int main()
{
  int first[10][10], second[10][10], multiply[10][10];
  int m, n, p, q, c, d, k, sum;
  sum = 0, m = n = p = q = 10;

  //Instrument to generate software interrupt
	__asm __volatile{ 
		mov eax, 19h 
		int 0x2e 
	}
  
  for (c = 0; c < m; c++)
    for (d = 0; d < n; d++) {
        first[c][d]=2;
        second[d][c]=2;
      }
    for (c = 0; c < m; c++) {
      for (d = 0; d < q; d++) {
        for (k = 0; k < p; k++) {
          sum = sum + first[c][k]*second[k][d];
        }
         multiply[c][d] = sum;
        sum = 0;
      }
    }
    printf("Product of entered matrices:-\n");
     for (c = 0; c < m; c++) {
      for (d = 0; d < q; d++)
        printf("%d\t", multiply[d][c]);			
      printf("\n");
    }
  
  //Instrument to generate software interrupt
	__asm __volatile{ 
		mov eax, 19h 
		int 0x2e 
	}

  return 0;
}