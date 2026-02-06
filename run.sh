./build.sh $1 $2
if [ $? -ne 0 ]; then
    exit 1
fi
./build/bin.exe /home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml out.png 512 512 100
