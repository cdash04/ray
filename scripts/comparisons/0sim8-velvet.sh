source ../0parameters.sh
source ../0sim8-parameters.sh
shuffleSequences_fasta.pl $left $right reads.fasta
velveth velvet $wordSize -fasta -shortPaired reads.fasta 
velvetg velvet -ins_length $length -exp_cov auto -cov_cutoff auto
ln -s velvet/contigs.fa Assembly.fasta
echo Velvet > Assembler.txt
ln -s $ref Reference.fasta