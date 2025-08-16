COMMAND="gmx mdrun -s benchMEM.tpr -nsteps 200 -nb gpu -notunepme -pme cpu -pmefft cpu -bonded gpu -update cpu -ntmpi 1"
