COMMAND="gmx mdrun -s benchMEM.tpr -nsteps 200 -nb gpu -notunepme -pme gpu -pmefft gpu -bonded gpu -update cpu -ntmpi 1"
