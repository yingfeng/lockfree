if [ "$#" -ne "1" ]; then
    echo "Usage: $0 MACHINENAME (as found in plaf.bash)"
    exit
fi
machine=$1

cd ..
path=`pwd`
echo "PATH=$path"
datadir=${path}/output.${machine}.${exp}
outfile=${datadir}/${exp}.csv
header=${path}/scripts/${exp}.header
errfile=${datadir}/errors.out