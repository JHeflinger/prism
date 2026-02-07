./build.sh $1 $2
if [ $? -ne 0 ]; then
    exit 1
fi
./build/bin.exe
