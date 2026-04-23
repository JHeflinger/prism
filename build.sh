# create all build directories if it does not exist
if [ ! -d "build" ]; then
    mkdir "build"
fi
cd build
if [ ! -d "shaders" ]; then
    mkdir "shaders"
fi
if [ ! -d "cache" ]; then
    mkdir "cache"
fi
if [ ! -d "expanded" ]; then
    mkdir "expanded"
fi
cd cache
if [ ! -d "shaders" ]; then
    mkdir "shaders"
fi
cd ..
cd ..

# download simp
if [ ! -f "build/simp_linux.bin" ] || [ "$1" == "-u" ] || [ "$2" == "-u" ]; then
    if [ -f "build/simp_linux.bin" ]; then
        echo "Updating simp importer..."
        rm build/simp_linux.bin
    else
        echo "Downloading simp importer..."
    fi
    cd build
    curl -L -s -o "simp_linux.bin" "https://github.com/JHeflinger/simp/raw/refs/heads/main/bin/simp_linux.bin"
    chmod +x simp_linux.bin
    cd ..
fi

# expand shaders
./build/simp_linux.bin shaders build/expanded

# compile shaders
echo "Compiling shaders..."
startTime=$(date +%s%N)
SHADERS_UP_TO_DATE="true"
while IFS= read -r file; do
    filename=$(basename "$file")
    if [ ! -f "build/cache/shaders/$filename" ]; then
        SHADERS_UP_TO_DATE="false"
        echo -e "- [$filename] \033[33m(compiling...)\033[0m"
        glslc $file -o "build/shaders/$filename.spv"
        if [ $? -ne 0 ]; then
            echo -e "Building shader \033[31mfailed\033[0m"
            exit 1
        fi
        echo -e "\033[1A\033[0K- [$filename] \033[32mOK\033[0m"
        cp $file "build/cache/shaders/$filename"
    else
        if ! cmp -s $file "build/cache/shaders/$filename"; then
            SHADERS_UP_TO_DATE="false"
            echo -e "- [$filename] \033[33m(compiling...)\033[0m"
            glslc $file -o "build/shaders/$filename.spv"
            if [ $? -ne 0 ]; then
                echo -e "Building shader \033[31mfailed\033[0m"
                exit 1
            fi
            echo -e "\033[1A\033[0K- [$filename] \033[32mOK\033[0m"
            cp $file "build/cache/shaders/$filename"
        fi
    fi
done < <(find "build/expanded" -type f \( -name "*.vert" -o -name "*.frag" -o -name "*.comp" \))
endTime=$(date +%s%N)
elapsed=$(((endTime - startTime) / 1000000))
hh=$((elapsed / 3600000))
mm=$(((elapsed % 3600000) / 60000))
ss=$(((elapsed % 60000) / 1000))
cc=$((elapsed % 1000))
if [ "$SHADERS_UP_TO_DATE" == "true" ]; then
    echo -e "\033[1A\033[0KShaders are currently \033[32mup to date\033[0m"
else
    echo -e "\033[32mFinished\033[0m building shaders in ${hh}:${mm}:${ss}.${cc}"
fi

# download builder
if [ ! -f "build/tiny_linux.bin" ] || [ "$1" == "-u" ] || [ "$2" == "-u" ]; then
    if [ -f "build/tiny_linux.bin" ]; then
        echo "Updating tiny builder..."
        rm build/tiny_linux.bin
    else
        echo "Downloading tiny builder..."
    fi
    cd build
    curl -L -s -o "simp_linux.bin" "https://github.com/JHeflinger/tiny/raw/refs/heads/main/bin/tiny_linux.bin"
    chmod +x tiny_linux.bin
    cd ..
fi

# run builder
PROD=""
if [ "$1" == "-p" ] || [ "$2" == "-p" ] || [ "$3" == "-p" ]; then
    PROD="-prod"
fi
./build/tiny_linux.bin $PROD
if [ $? -ne 0 ]; then
    exit 1
fi
