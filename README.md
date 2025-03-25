[PRISM](https://github.com/JHeflinger/prism) - Cross-platform C Renderer/Modeler
============================================================================

## INFO

Prism is a toy renderer / modeler for testing out different graphics techniques in raytracing and raymarching! Using Vulkan, it supports Windows and Linux systems on all levels by doing raytracing through compute shaders.
> **_NOTE:_**  Prism is not meant to be a commerical product, and should be viewed as a learning tool for graphics programming in Vulkan. 

## STATUS

WIP - Prism is not production ready and may have bugs or issues. Download and use at your own risk!

## REQUIREMENTS

While Prism uses Vulkan and other supplemental libraries such as raylib or cglm, all external libraries are included in the repository or via subrepositories. You can install the Vulkan SDK if you wish to enable the Vulkan debug handler, but other than that all you will need is gcc to compile. If you are on Linux, you will also have to install GLSLC to compile the shaders, or you can bug me to include the pre-built binary in the repo and add it to the build scripts.

## BUILDING

First ensure you have cloned the repo along with any subrepos.
```
git clone https://github.com/JHeflinger/prism.git --recursive
cd prism
```
If you have already cloned it, you can also download the subrepos by running the following in the repo's working directory:
```
git submodule update --recursive --init
```
If you're on Linux, you can compile and run the program using `run.sh`
```
./run.sh
```
If you're on Windows, you can compile and run the program using `run.bat`
```
./run.bat
```
You can also use the `shell.sh` or `shell.bat` scripts to enter a mini shell program with additional commands intended for development use. Note that these scripts utilize Python, so make sure you have that installed.

> **_NOTE:_**  Prism is only cross-platform for Linux and Windows systems - Mac is not supported.