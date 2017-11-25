rm -f $outfile
rm -f $errfile

## print csv header
preheader=`cat $header | tr -d "\r\n"`
echo -n "$preheader" >> $outfile
for f in "${fields[@]}" ; do
	echo -n ,$f >> $outfile
done
for f in "${fields2[@]}" ; do
	echo -n ,$f >> $outfile
done
for fn in "${fields3[@]}" ; do
	#f=`echo $fn | cut -d"~" -f1`
	echo -n ,$fn >> $outfile
done
echo -n ",obj1type,obj1size,obj1allocated,obj2type,obj2size,obj2allocated" >> $outfile
echo -n ",filename" >> $outfile
echo -n ",cmd" >> $outfile
echo -n ",machine" >> $outfile
echo >> $outfile

cd $datadir

cat $outfile

## print csv contents
cnt1=0
cnt2=0
for counting in 1 0 ; do
for x in `ls *.data` ; do
	if [ $counting -eq 1 ]; then
		cnt1=`expr $cnt1 + 1`
		continue
	fi
	cnt2=`expr $cnt2 + 1`

	cmd=`cat $x | head -1`

	## prepend information in filename
	fnamedata=`echo $x | tr "." ","`
	echo -n "$fnamedata" >> $outfile

	## grep fields from file
	for f in "${fields[@]}" ; do
		nlines=`cat $x | grep "$f" | wc -l` ; if [ $nlines -ne 1 ]; then echo "WARNING: grep returned $nlines lines for field $f in file $x" >> $errfile ; fi
		echo -n , >> $outfile
		cat $x | grep "$f" | cut -d":" -f2 | cut -d" " -f2 | tr -d " _abcdefghijklmnopqrstuvwxyz\r\n" >> $outfile
	done

	## grep second type of fields (x=###)
	for f in "${fields2[@]}" ; do
		nlines=`cat $x | grep "$f=" | wc -l` ; if [ $nlines -ne 1 ]; then echo "ERROR: grep returned $nlines lines for field $f in file $x" >> $errfile ; fi
		echo -n , >> $outfile
		cat $x | grep "$f=" | cut -d"=" -f2 | tr -d "\n\r" >> $outfile
	done

	## grep third type of fields
	for fn in "${fields3[@]}" ; do
		f=`echo $fn | cut -d"~" -f1`
		n=`echo $fn | cut -d"~" -f2`
		echo -n , >> $outfile
		${path}/scripts/add `cat $x | grep -E "${f}[ ]+:" | cut -d":" -f2 | cut -d" " -f$n | tr -d "\r" | tr "\n" " "` >> $outfile
		#echo ; echo ; echo "f=$f" ; cat $x | grep -E "${f}[ ]+:" | cut -d":" -f2 | cut -d" " -f$n | tr -d "\r" | tr "\n" " " ; echo ; echo
	done

    ## add memory allocation info
    echo -n , >> $outfile ; cat $x | grep "recmgr status" | tr "\n" "~" | cut -d"~" -f1 | cut -d" " -f10 | tr -d "\n" >> $outfile
    echo -n , >> $outfile ; cat $x | grep "recmgr status" | tr "\n" "~" | cut -d"~" -f1 | cut -d" " -f7  | tr -d "\n" >> $outfile
    echo -n , >> $outfile ; cat $x | grep "allocated  "   | tr "\n" "~" | cut -d"~" -f1 | cut -d":" -f2  | cut -d" " -f5 | tr -d "\n" >> $outfile
    echo -n , >> $outfile ; cat $x | grep "recmgr status" | tr "\n" "~" | cut -d"~" -f2 | cut -d" " -f10 | tr -d "\n"  >> $outfile
    echo -n , >> $outfile ; cat $x | grep "recmgr status" | tr "\n" "~" | cut -d"~" -f2 | cut -d" " -f7  | tr -d "\n" >> $outfile
    echo -n , >> $outfile ; cat $x | grep "allocated  "   | tr "\n" "~" | cut -d"~" -f2 | cut -d":" -f2  | cut -d" " -f5 | tr -d "\n" >> $outfile

	## add filename, command and machine
	echo -n ",\"$x\"" >> $outfile
	echo -n ",\"$cmd\"" >> $outfile
	echo -n ",$machine" >> $outfile

	echo >> $outfile

	# debug output
	echo -n "$cnt2 / $cnt1: "
	cat $outfile | tail -1
done
done

cat $errfile
