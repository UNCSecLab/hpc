
This package consists of 20 variants of the original **rep stosb** program that was used by [Weaver & McKee](https://github.com/deater/deterministic/blob/master/static/sample_code/). These benchmarks comprise all 10 string operations, which include one-byte instructions (lodsb, stosb, movsb, scasb, cmpsb) and two-byte instructions (lodsw, stosw, movsw, scasw, cmpsw). These instructions perform load, store, copy, scan, and comparison operations. Each of these variants executes one string operation 1 million times. 

## Requirements: 
- Runs on Linux OS. 
- AS - GNU assembler

## How to build:
- To build an individual program e.g., **rep_stosb.s**

```bash
  as -o $rep_stosb.o $rep_stosb.s
  ld -o $rep_stosb $rep_stosb.o
```
- Run **build.sh** to compile all the programs.