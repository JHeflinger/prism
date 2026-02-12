./build.sh $1 $2 $3
if [ $? -ne 0 ]; then
    exit 1
fi
#./build/bin.exe ~/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml out.png 512 512 10 -1.0 true true
./build/bin.exe
