./build.sh $1 $2 $3
if [ $? -ne 0 ]; then
	exit 1
fi
./build/bin.exe 400 200
