[![image](misc/SRecomp.png)](https://github.com/ExpansionPak/SuperRecomp)

# SuperRecomp
Tool to statically recompile SNES games into native executables. The project is in a very early stage. So C code generation is still experimental and messy sometimes.

Demonstration of the tool itself using a clean dump of [Super Mario World](https://en.wikipedia.org/wiki/Super_Mario_World) without a 512-byte header.

https://github.com/user-attachments/assets/bc8e781c-8fc8-4a88-9fce-41251fe4a63e

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
cmake --build . --target RecompTool
```
Or. If you have Ninja. Enter `ninja RecompTool` instead pf `cmake --build . --target RecompTool`

# Usage
```
./RecompTool <path/to/rom>
```

# A TOOL MADE BY:
[![image](https://avatars.githubusercontent.com/u/271502946?s=400&u=ae83108f7c426d94cedc804ac687f9c48e117173&v=4)](https://github.com/ExpansionPak)
