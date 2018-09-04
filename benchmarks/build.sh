!/bin/bash

declare -a arr=("rep_lodsb" "rep_stosb" "rep_cmpsb" "rep_movsb" "rep_scasb" "loop_lodsb" "loop_stosb" "loop_cmpsb" "loop_movsb" "loop_scasb" "rep_lodsw" "rep_stosw" "rep_cmpsw" "rep_movsw" "rep_scasw" "loop_lodsw" "loop_stosw" "loop_cmpsw" "loop_movsw" "loop_scasw")

for i in "${arr[@]}"
do
	#compilation-commands
	as -o $i.o $i.s
	ld -o $i $i.o
done
