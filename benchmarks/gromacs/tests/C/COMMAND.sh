COMMAND="gmx mdrun -s benchMEM.tpr -nsteps 200 -nb gpu -notunepme -pme gpu -pmefft cpu -bonded cpu -update cpu -ntmpi 1"
