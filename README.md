# SuperRecomp
Tool to statically recompile SNES games into native executables. The project is in a very early stage. So the only thing as of now is ROM file reading.

# UltraRecomp
The first "Target" in the "UltraRecomp" series. Many more (including GameCube, Wii U, PS1 and PS1. And maybe also Xbox 360)  will come.

# Instructions
First. Download [MSYS2](https://www.msys2.org/) and [CMake](https://cmake.org/download/). [Ninja](https://github.com/ninja-build/ninja/releases) is also heavily recommended.

Open the UCRT64 terminal and enter
```
git clone https://github.com/ExpansionPak/SuperRecomp.git
```
Now. Inside your cloned repo. Enter
```
mkdir build && cd build
cmake ..
cmake --build .
```
Or. If you have Ninja. Enter `ninja` instead pf `cmake --build .`

# Usage
```
./SuperRecomp
```