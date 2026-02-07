Setting Up Your Build Environment
======================================================================

Linux Environment
======================================================================

This code is developed under Linux and we are assuming that you have
some kind of Linux (or other Unix) environment as a base.
From there we will install dependencies and the cross compiler
needed to build Munix.

- The reference build environment is Ubuntu 24.04 LTS (Noble Numbat).

    - If you use this distro, then you should be able to follow this guide
      step-by-step with no issues.

    - If you use another distro or another version, you may have to make
      some tweaks to the procedure.

- If you are running Windows,
    you should be able to build the code using a Linux environment in WSL.
    See [Building on Windows with WSL2](15-build-env-wsl2.md).

- If you are running a Mac you,
    should be able to run the build in the Mac terminal.
    See [Building on a Mac](18-build-env-mac.md).

Once you have a base environment ready,
come back and follow the rest of this guide.

If you run into trouble,
check the [Build FAQ](19-build-faq.md).
Maybe your issue already has a workaround.

Linux Packages to Install
======================================================================

First we need to install several packages from our package manager.
The examples here use `apt`.
If you use a different distro with a different package manager,
you will have to adapt the install commands and package names to
your package manager.

Build essentials (C compiler)
----------------------------------------------------------------------

First you need a normal C toolchain. On apt-based systems (Debian, Ubuntu,
etc.), there is a package called `build_essential` that handles that.

<!-- Markdown note: I am using .py syntax because Doxygen's Markdown parser
    does not support bash/shell scripts, and Python is the closest available
    (it also has '#' for a comment Marker). -->

```{.py}
# Install base C compiler and other essential tools.
sudo apt install build-essential
```

For Building the Cross Compiler {#prereqs_cross}
----------------------------------------------------------------------

For a cross compiler, we need to compile two GNU packages from source:

1. [GNU binutils](https://www.gnu.org/software/binutils/)
    --- low-level utilities for manipulating code and object files,
        including an assembler, linker, and tools like `readelf` and `objdump`

2. [GCC, the GNU Compiler Collection](https://gcc.gnu.org/)
    --- The actual C compiler.

To build these we will need a handful of GNU support tools and libraries
that should be available in our distro's package manager.

### Required packages

```{.py}
# GNU Multiple-Precision Math libraries
# These help the compiler implement math operations.
#   libgmp-dev  --- GNU Multiple-Precision math (integers)
#   libmpfr-dev --- GNU Multiple-Precision Floating-point Rounding
#   libmpc-dev  --- GNU Multiple-Precision Complex math
sudo apt install libgmp-dev libmpfr-dev libmpc-dev

# Compiler generators
# These help the compiler parse different languages.
#   flex        --- Lexical analysis
#   bison       --- Syntax/grammar parsing
sudo apt install flex bison

# Documentation tools
#   texinfo     --- GNU's document processing system
sudo apt install texinfo
```

### Optional packages

The cURL tool will be useful for downloading source tarballs from
the command line. But you can also use your browser to download them instead.

```{.py}
# Command-line download tool and SSL certificates
# Many systems come with these pre-installed. You may not need them.
#   curl            --- cURL, a tool that can download via many protocols
#   ca-certificates --- Common public certificates for SSL
sudo apt install curl ca-certificates
```

For Building GDB and GRUB (Non-x86/Non-Linux Only)
----------------------------------------------------------------------

You will need a bootloader (GRUB) and a debugger (GDB).

- If you are on x86, then the GDB that was installed with `build-essentials`
    will be able to understand and debug the i386 code as well.

- If you are on Linux on x86, then your distro probably uses GRUB as its
    bootloader, and the system's GRUB should also be able to create
    an i386 boot disk.

If you are not on x86 and/or not booting with GRUB, then you probably
need to build those from source too.
In order to build GDB and GRUB, you will need the following packages.

```{.py}
# For building GDB
#   pkg-config          --- A source-code configuration utility used by GDB
#   libncurses-dev      --- Classic 'curses' terminal library, for text UIs
#   libsource-highlight-dev --- Syntax highlighting in GDB's source views
sudo apt install pkg-config libncurses-dev libsource-highlight-dev

# For building GRUB
#   python3     --- Python interpreter (probably already installed)
sudo apt install python3
```

For Building the Munix OS {#prereqs_make}
----------------------------------------------------------------------

Finally, you need a few more packages for our build,
beyond the cross compiler.

### Required to build and run the boot image

```{.py}
# For building and running the bootable disk image
#   cpio        --- Creates the cpio ramdisk archive
#   xorriso     --- Creates CD-ROM ISO images, used by GRUB
#   qemu-system-i386    --- i386 emulator
sudo apt install cpio xorriso qemu-system-i386
```

### Optional tooling {#prereqs_make_optional}

```{.py}
# Recommended for your editor
#   universal-ctags --- Old-school C code indexer, used by vim, emacs, et al.
#   bear            --- New tool used by VSCode, LSP-enabled editors, and clang
sudo apt install universal-ctags bear

# Required to generate HTML documentation
#   doxygen         --- C documentation generator
#   graphviz        --- Graph drawing tool, used by Doxygen
sudo apt install doxygen graphviz
```

Building the Cross Compiler
======================================================================

binutils and GCC
----------------------------------------------------------------------

Once you install the prerequisite libraries (see [above](#prereqs_cross})),
we can build the cross compiler from source.

### Set variables {#cross_vars}

First, let's set some variables we will refer to later:

```{.py}
# Target architecture: i386 with a generic ELF-based ABI
export CROSS_TARGET=i386-elf

# Versions
# As of January 2026, these versions match the versions
# of the system compilers on Ubuntu 24.04 LTS (noble).
export BINUTILS_VERSION=2.40
export GCC_VERSION=13.4.0
export GDB_VERSION=15.2
export GRUB_VERSION=2.06

# GNU FTP mirror site
#   https://ftpmirror.gnu.org will automatically choose one close to you
export GNU_MIRROR=https://ftpmirror.gnu.org
```

### Choose paths

You need three paths:

1. A source directory to download and install source code to
2. A separate working directory to build the code in
3. A more permanent director to install the cross compiler to

Let's say you are putting all your work for this course under
your home directory, in `~/projects/uit-inf2203/`.
You could do something like this:

```{.py}
export CROSS_SRC=$HOME/projects/uit-inf2203/cross_src
export CROSS_BUILD=$HOME/projects/uit-inf2203/cross_src
export CROSS_INSTALL=$HOME/projects/uit-inf2203/cross_install

# Make sure the directories exist.
mkdir -p $CROSS_SRC $CROSS_BUILD $CROSS_INSTALL
```

### Download sources

```{.py}
# Switch to source directory.
cd $CROSS_SRC

# If you have cURL installed, you can grab the tarballs from the command line.
curl -LfO $GNU_MIRROR/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz
curl -LfO $GNU_MIRROR/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.gz

# Or, print and copy the URLs and open them in your browser.
echo $GNU_MIRROR/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz
echo $GNU_MIRROR/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.gz
```

### Unpack sources

```{.py}
# Switch to source directory.
cd $CROSS_SRC

tar -xzf binutils-$BINUTILS_VERSION.tar.gz
tar -xzf gcc-$GCC_VERSION.tar.gz
```

### Build and install binutils

The _configure_ step sets up the build:
what target architecture we want,
where to install the compiler, etc.

```{.py}
# Switch to the build directory.
cd $CROSS_BUILD

# Create a binutils directory.
mkdir binutils-$BINUTILS_VERSION-$CROSS_TARGET
cd binutils-$BINUTILS_VERSION-$CROSS_TARGET

# From the build directory, run the configure script in the source directory.
$CROSS_SRC/binutils-$BINUTILS_VERSION/configure \
                --target=$CROSS_TARGET --prefix=$CROSS_INSTALL --disable-nls

# Build.
make -j `nproc --ignore=2`

# Install.
make install
```

Once you have binutils installed, you should see files in your
`$CROSS_INSTALL` directory. You should especially see the binutils
programs under `$CROSS_INSTALL/bin`:

```
ls $CROSS_INSTALL/bin
# i386-elf-as
# i386-elf-ld
# i386-elf-nm
# i386-elf-objdump
# i386-elf-readelf

which i386-elf-as
# /home/user/projects-uit-inf2203/cross_install/bin/i386-elf-as
```

### Add binutils to your $PATH

Now, in order to use these binutils, add the binutils directory to your PATH:

- To add it to the path in the current terminal session, run this:

    `export PATH=$CROSS_INSTALL/bin:$PATH`

- To have it automatically added to your path when you log in,
    open the file `.profile` in your home directory and add this line
    (be sure to change it to the actual location of your `$CROSS_INSTALL`
     directory):

    `export PATH=$HOME/projects/uit-inf2303/cross_install/bin`

### Build and install GCC

Now that you have binutils available, you can compile GCC.
The procedure is the same as with binutils: `configure` and then `make`

```{.py}
# Switch to the build directory.
cd $CROSS_BUILD

# Create a GCC directory.
mkdir gcc-$GCC_VERSION-$CROSS_TARGET
cd gcc-$GCC_VERSION-$CROSS_TARGET

# From the build directory, run the configure script in the source directory.
$CROSS_SRC/gcc-$GCC_VERSION/configure \
                --target=$CROSS_TARGET --prefix=$CROSS_INSTALL --disable-nls \
                --enable-languages=c --without-headers

# Build GCC.
# NB: This step takes on the order of 5--10 minutes or more,
#       depending on your PC.
make all-gcc                -j `nproc --ignore=2`
make all-target-libgcc      -j `nproc --ignore=2`

# Install.
make install-gcc
make install-target-libgcc
```

Now you should have the cross compiler installed in `$CROSS_INSTALL/bin`:

```{.ps}
ls $CROSS_INSTALL/bin
# ... i386-elf-gcc ...

which i386-elf-gcc
# /home/user/projects/uit-inf2203/cross_install/bin/i386-elf-gcc
```

Building GDB and/or GRUB if Necessary
----------------------------------------------------------------------

Building GDB or GRUB is like the others:
download, extract, configure, `make`.
Just make sure you still have the
[variables and paths set from before](#cross_vars).

### Build GDB if not on i386

```{.py}
# Switch to source directory.
cd $CROSS_SRC

# Download and extract source.
curl -LfO $GNU_MIRROR/gnu/gdb/gdb-$GDB_VERSION.tar.gz
tar -xzf gdb-$GDB_VERSION.tar.gz

# Switch to build directory.
cd $CROSS_BUILD
mkdir gdb-$GDB_VERSION-$CROSS_TARGET
cd gdb-$GDB_VERSION-$CROSS_TARGET

# From build directory, run source directory's configure script.
$CROSS_SRC/gdb-$GDB_VERSION/configure \
                --target=$CROSS_TARGET --prefix=$CROSS_INSTALL --disable-nls

# Make and install.
make -j `nproc --ignore=2`
make install

# Check.
which i386-elf-gdb
# /home/user/projects/uit-inf2303/cross_install/bin/i386-elf-gdb
```

### Build GRUB if system does not already use GRUB

```{.py}
# Switch to source directory.
cd $CROSS_SRC

# Download and extract.
curl -LfO $GNU_MIRROR/gnu/grub/grub-$GRUB_VERSION.tar.gz
tar -xzf grub-$GRUB_VERSION.tar.gz

# Switch to build directory.
cd $CROSS_BUILD
mkdir grub-$GRUB_VERSION-$CROSS_TARGET
cd grub-$GRUB_VERSION-$CROSS_TARGET

# From build directory, run source directory's configure script.
$CROSS_SRC/grub-$GRUB_VERSION/configure \
                --target=$CROSS_TARGET --prefix=$CROSS_INSTALL --disable-nls \
                --disable-werror

# Make and install.
make -j `nproc --ignore=2`
make install

# Check.
which grub-mkrescue
/home/ubuntu/projects/uit-inf2203/cross_install/bin/grub-mkrescue
```

Building and Booting the OS
======================================================================

Once the cross compiler and other packages are installed,
you are ready to build the image.

Basic Build and Boot
----------------------------------------------------------------------

First switch to the precode directory,
then run `make` to build the image.

```{.py}
# Build the boot image.
make image

# This creates an ISO image file.
ls out/i386-elf/bootimage.iso
```

With the image created, you can launch it in the emulator.

```{.py}
# Boot in the emulator.
# Use the QEMU UI to the serial view to see the serial output.
qemu-system-i386 -cdrom out/i386-elf/bootimage.iso -boot d

# Try booting in the emulator with serial I/O redirected to your terminal.
# That way you can see both the serial I/O and the display at the same time.
qemu-system-i386 -cdrom out/i386-elf/bootimage.iso -boot d -serial stdio
```

If you have your cross compiler and your emulator installed in the same
environment (i.e. you haven't done anything fancy with containers),
then you can use the `make run` target to build and then launch with
one command.

```{.py}
# Convenience target to build and launch with one command.
make run

# Use the QEMUFLAGS variable to pass additional arguments to QEMU.
make run QEMUFLAGS="-serial stdout"
```

Debugging with GDB
----------------------------------------------------------------------

QEMU has an option to connect to the GDB debugger and let you debug
the OS inside the emulator.

If you have QEMU and GDB installed in the same environment,
then you can use the `make debug` target to build, launch, and debug
all with one command.

```{.py}
# Convenience target to build and debug.
make debug

# Then in the GDB console, try setting a breakpoint at kernel start
# and then stepping through some instructions.
break _start
continue
step
step
# ...
```

If you have QEMU and GDB in different environments (e.g. if you are using
containers or virtual machines),
then you should still be able to connect GDB and QEMU with a socket.

```{.py}
# In one terminal, launch QEMU, listening for GDB on a socket.
qemu-system-i386 -cdrom out/i386-elf/bootimage.iso -boot d \
    -gdb tcp:localhost:1234 -S

# In another, launch GDB with a command to connect to the same socket.
gdb out/i386-elf/kernel/kernel -ex "target remote localhost:1234"
```

Other Make Targets
----------------------------------------------------------------------

```{.py}
make            # Default: Run tests and build image

make dev        # Set up development tooling
make doc        # Generate documentation HTML (Doxygen)
make test       # Run tests
make image      # Build bootable disk image
make all        # Build all: doc, dev, test, image

make run        # Build image and launch it in an emulator
make debug      # Build image and debug it in an emulator

make clean      # Remove most built files
make distclean  # Remove all non-source files
```

Generating Documentation with Doxygen
======================================================================

If you have [Doxygen and graphviz installed](#prereqs_make_optional),
you can generated a cross-referenced documentation of the codebase
in HTML that you can open in your browser.

```{.py}
# Generate documentation
make doc

# Open the generated HTML in your browser.
# Use this command to print the full path of the index.html file,
# and then paste it into your browser window.
readlink -f out/doxygen/html/index.html
```

Configuring Your IDE
======================================================================

C tooling can be tricky, since the C compiler takes so many flags
to specify different include directories and `#define` macros.
This is especially true for operating systems development,
since we are not using the typical C standard library headers that are
installed on the host system.

The Munix build system here supports two types of tooling:

- [CTAGS](https://en.wikipedia.org/wiki/Ctags),
    an old-school indexing system for C code used by editors like
    vim and emacs

- [compile_commands.json](https://clang.llvm.org/docs/JSONCompilationDatabase.html),
    a list of the specific commands used (and their flags),
    which is used by VSCode and other
    editors that support the Language Server Protocol (LSP)

To generate these indexes/databases, run this make target:

```{.py}
# Build tooling databases.
make dev
```

- To make the CTAGS database, the [ctags tool](https://ctags.io/) must be
    installed (Ubuntu: `apt install universal-ctags`)

- To make the compiler_commands.json database,
    you must have your cross compiler set up to build the OS
    (see above) and you must have the
    [`bear` utility](https://github.com/rizsotto/Bear)
    installed
    (Ubuntu: `apt install bear`).

- If either of these tools is missing, the `make dev` command will simply
    skip that step.

compiler\_commands.json: Editor setup
----------------------------------------------------------------------

Some IDEs will find the `compile_commands.json` file automatically.
Others require some settings.

### VSCode

1. Open the Microsoft C/C++ Extension's IntelliSense Configurations screen.

    1. Open a C code file in the project.
    2. In the status bar, next to the language setting ("C"),
        there is a place to select the C/C++ Configuration to use.
        Click this.
    3. A pop-up will ask you to "Select a Configuration".
        Click "Edit Configurations (UI)"

2. Expand the "Advanced Settings" at the bottom.

3. Find the "Compile Commands" heading.

4. In the compile commands box, add this path:

    `${workspaceFolder}/compile_commands.json`
