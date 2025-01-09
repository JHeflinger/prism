# audit codebase
python scripts/help.py audit

# create build directory if it does not exist
if [ ! -d "build" ]; then
	mkdir "build"
fi

# initialize vars for building
SRC_DIR="src"
INCLUDES=""
SOURCES=""
LIBS=""
LINKS=""

# production build flags
PROD=""
if [ "$1" == "prod" ]; then
	echo "Optimizing for production build..."
	PROD="-O3 -DPROD_BUILD"
fi

# get src includes and sources
while IFS= read -r dir; do
	INCLUDES="$INCLUDES -I$dir"
done < <(find "$SRC_DIR" -type d)
while IFS= read -r file; do
	SOURCES="$SOURCES $file"
done < <(find "$SRC_DIR" -type f -name "*.c")

# add raylib vendor
INCLUDES="$INCLUDES -Ivendor/raylib/include"
LINKS="$LINKS -l:linux_amdx64_libraylib.a"
LINKS="$LINKS -lm"
LINKS="$LINKS -lpthread"
LINKS="$LINKS -lGL"
LINKS="$LINKS -lGLU"
LIBS="$LIBS -Lvendor/raylib/lib"

# add EasyObjects vendor
INCLUDES="$INCLUDES -Ivendor/EasyObjects/include"
SOURCES="$SOURCES vendor/EasyObjects/include/easymemory.c"

# add cglm vendor
INCLUDES="$INCLUDES -Ivendor/cglm/include"

# compile
echo "Building prism..."
startTime=$(date +%s%N)
gcc -Wall -Wextra$SOURCES$INCLUDES$LIBS$LINKS -o build/prism $PROD
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
echo -e "\033[32mFinished\033[0m building in ${hh}:${mm}:${ss}.${cc}"
