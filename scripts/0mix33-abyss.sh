source ../0parameters.sh
source ../0mix32-parameters.sh
source ../0mix31-parameters.sh
removeBreaks.py $r4541 t1
removeBreaks.py $r4542 t2
removeBreaks.py $r4543 t3
removeBreaks.py $r4544 t4
removeBreaks.py $r4545 t5
cat t1 t2 t3 t4 t5 > reads.fasta
ABYSS -k$wordSize reads.fasta $r1 $r2 $r3 -o Assembly.fasta
echo ABySS>Assembler.txt
ln -s $ref Reference.fasta
