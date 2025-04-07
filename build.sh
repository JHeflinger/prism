# audit codebase
python scripts/help.py audit

# total timing
t_startTime=$(date +%s%N)

# initialize vars for building
SRC_DIR="src"
CDIR="$(pwd)"
INCLUDES=""
SOURCES=""
OBJECTS=""
LIBS=""
LINKS=""

# production build flags
PROD=""
if [ "$1" == "prod" ]; then
	echo "Optimizing for production build..."
	PROD="-O3 -DPROD_BUILD"
fi

# create build directory if it does not exist
if [ ! -d "build" ]; then
	mkdir "build"
fi

# create all build directories if it does not exist
cd build
if [ ! -d "shaders" ]; then
	mkdir "shaders"
fi
if [ ! -d "cache" ]; then
	mkdir "cache"
fi
if [ ! -d "vendor" ]; then
	mkdir "vendor"
fi
cd cache
if [ ! -d "shaders" ]; then
	mkdir "shaders"
fi
if [ ! -d "src" ]; then
	mkdir "src"
fi
cd ..
cd ..

# set up cache folders
find "$SRC_DIR" -type d | while read -r SUBPATH; do
	REL="${SUBPATH#$CDIR/$SRC_DIR}"
	DESTDIR="build/cache/$REL"
	if [ ! -d "$DESTDIR" ]; then
		mkdir -p "$DESTDIR"
	fi
done

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
done < <(find "shaders" -type f \( -name "*.vert" -o -name "*.frag" -o -name "*.comp" \))
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

# get includes
while IFS= read -r dir; do
	INCLUDES="$INCLUDES -I$dir"
done < <(find "$SRC_DIR" -type d)

# add raylib vendor
INCLUDES="$INCLUDES -Ivendor/raylib/include"
LINKS="$LINKS -l:linux_amdx64_libraylib.a"
LINKS="$LINKS -lm"
LINKS="$LINKS -lpthread"
LINKS="$LINKS -lGL"
LINKS="$LINKS -lGLU"
LIBS="$LIBS -Lvendor/raylib/lib"

# add stb_image
INCLUDES="$INCLUDES -Ivendor/stb_image/include"

# add glfw and vulkan
LINKS="$LINKS -lglfw"
LINKS="$LINKS -lvulkan"

# add EasyObjects vendor
INCLUDES="$INCLUDES -Ivendor/EasyObjects/include"
SOURCES="$SOURCES vendor/EasyObjects/include/easymemory.c"

# add EasyThreads vendor
INCLUDES="$INCLUDES -Ivendor/EasyThreads/include"

# add EasyLogger vendor
INCLUDES="$INCLUDES -Ivendor/EasyLogger/include"

# add cglm vendor
INCLUDES="$INCLUDES -Ivendor/cglm/include"

# compile vendor
startTime=$(date +%s%N)
if [ ! -z "$SOURCES" ]; then
	if [ ! -f "build/vendor/vendor.o" ]; then
		echo "Compiling vendors..."
		gcc -Wall -Wextra -Wno-unused-parameter -c$SOURCES$INCLUDES$LIBS$LINKS -o build/vendor/vendor.o $PROD
		if [ $? -ne 0 ]; then
			echo -e "Building vendors \033[31mfailed\033[0m"
			exit 1
		fi
		endTime=$(date +%s%N)
		elapsed=$(((endTime - startTime) / 1000000))
		hh=$((elapsed / 3600000))
		mm=$(((elapsed % 3600000) / 60000))
		ss=$(((elapsed % 60000) / 1000))
		cc=$((elapsed % 1000))
		echo -e "\033[32mFinished\033[0m compiling vendors in ${hh}:${mm}:${ss}.${cc}"
	fi
	OBJECTS="$OBJECTS build/vendor/vendor.o"
fi

# compile obj files
echo "Compiling sources..."
startTime=$(date +%s%N)
SOURCES_UP_TO_DATE="true"
FOUND_MAIN="false"
while IFS= read -r file; do
	REL="${file#$CDIR/$SRC_DIR}"
	DESTDIR="build/cache/$REL"
	filename=$(basename "$file")
	if [ "$filename" != "main.c" ]; then
		if [ ! -f $DESTDIR ]; then
			SOURCES_UP_TO_DATE="false"
			echo -e "- [$filename] \033[33m(compiling...)\033[0m"
			gcc -Wall -Wextra -Wno-unused-parameter -c $file$INCLUDES$LIBS$LINKS -o $DESTDIR.o $PROD
			if [ $? -ne 0 ]; then
				echo -e "Building source \"$filename\" \033[31mfailed\033[0m"
				exit 1
			fi
			echo -e "\033[1A\033[0K- [$filename] \033[32mOK\033[0m"
			cp $file $DESTDIR
		else
			if ! cmp -s $file $DESTDIR; then
				SOURCES_UP_TO_DATE="false"
				echo -e "- [$filename] \033[33m(compiling...)\033[0m"
				gcc -Wall -Wextra -Wno-unused-parameter -c $file$INCLUDES$LIBS$LINKS -o $DESTDIR.o $PROD
				if [ $? -ne 0 ]; then
					echo -e "Building source \"$filename\" \033[31mfailed\033[0m"
					exit 1
				fi
				echo -e "\033[1A\033[0K- [$filename] \033[32mOK\033[0m"
				cp $file $DESTDIR
			fi
		fi
		OBJECTS="$OBJECTS $DESTDIR.o"
	else
		FOUND_MAIN="true"
	fi
done < <(find "$SRC_DIR" -type f -name "*.c")
if [ "$SOURCES_UP_TO_DATE" == "true" ]; then
	echo -e "\033[1A\033[0KSources are currently \033[32mup to date\033[0m"
else
	endTime=$(date +%s%N)
	elapsed=$(((endTime - startTime) / 1000000))
	hh=$((elapsed / 3600000))
	mm=$(((elapsed % 3600000) / 60000))
	ss=$(((elapsed % 60000) / 1000))
	cc=$((elapsed % 1000))
	echo -e "\033[32mFinished\033[0m compiling sources in ${hh}:${mm}:${ss}.${cc}"
fi
if [ "$FOUND_MAIN" == "false" ]; then
	echo -e "\033[31mError\033[0m: unable to compile without a detected \"src/main.c\" file"
	exit 1
fi

# compile executable
echo "Building executable..."
startTime=$(date +%s%N)
gcc -Wall -Wextra -Wno-unused-parameter src/main.c$OBJECTS$INCLUDES$LIBS$LINKS -o build/prism $PROD
if [ $? -ne 0 ]; then
	echo -e "Build \033[31mFailed\033[0m"
	exit 1
fi
endTime=$(date +%s%N)
elapsed=$(((endTime - startTime) / 1000000))
hh=$((elapsed / 3600000))
mm=$(((elapsed % 3600000) / 60000))
ss=$(((elapsed % 60000) / 1000))
cc=$((elapsed % 1000))
echo -e "\033[32mFinished\033[0m building executable in ${hh}:${mm}:${ss}.${cc}"
endTime=$(date +%s%N)
elapsed=$(((endTime - t_startTime) / 1000000))
hh=$((elapsed / 3600000))
mm=$(((elapsed % 3600000) / 60000))
ss=$(((elapsed % 60000) / 1000))
cc=$((elapsed % 1000))
echo -e "\033[32mFinished\033[0m total build in ${hh}:${mm}:${ss}.${cc}"
