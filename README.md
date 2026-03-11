**Elegoo 3D Printer Firmware Project**

**Project Overview**

This is a complete 3D printer firmware project, including both the application layer program and the MCU firmware. It supports CoreXY motion structure and is designed for use with Elegoo 3D printers.

**Project Structure**
```
printer-realase-2602101855/
├── README.md                   # This documentation
├── Makefile                    # MCU firmware build configuration
├── mcu_build.sh                # MCU firmware build script
├── debugfile/                  # Debug configuration files
│   ├── printer.cfg             # Main printer configuration file
│   ├── printer_dsp.cfg         # DSP-related configuration
│   ├── product_test.cfg        # Product test configuration
│  └── temperature_sensors.cfg  # Temperature sensor configuration
├── elegoo/                     # Application layer program
│   ├── build.sh                # ARM platform build script
│   ├── CMakeLists.txt          # CMake build configuration
│   ├── main.cpp                # Main program entry point
│   ├── chelper/                # C helper libraries
│   ├── common/                 # Common components
│   ├── kinematics/             # Kinematics modules
│   ├── extras/                 # Extension modules
│  └── lib/                     # Third-party libraries
├── src/                        # MCU firmware source code
│   ├── Makefile                # MCU build configuration
│   ├── Kconfig                 # Configuration options
│   ├── *.c                     # MCU source files
│  └── */                       # Supported directories
├── lib/                        # Third-party library collection
│   ├── ar100/                  # AR100 processor support
│   ├── rp2040/                 # RP2040 processor support
│   ├── stm32*/                 # STM32 series support
│  └── ...                      # Other processor support
```

**System Architecture**

**Components**

- **Printer Application**: Manages overall control and G-code parsing
- **Toolhead MCU Firmware**: Controls printhead motion and extrusion
- **Heated Bed Sensor MCU Firmware**: Monitors heated bed status and controls leveling

**Building the Application Layer Program**

**Using CMake**
```
# Create installation directory 
mkdir /opt/toolchains

# Extract and install the cross-compilation toolchain
tar -xzvf prebuilt.tar.gz -C /opt/toolchains

# Enter the project directory
cd printer-realase-2602101855/elegoo

# Set environment variables
export STAGING_DIR=/opt/toolchains/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-glibc/toolchain/bin

# Build 
cmake -Bbuild
cmake --build build -j$(($(nproc) - 1))
```

**Building MCU Firmware**
```
# Install required build tools
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi

# Enter the project directory
cd printer-realase-2602101855/

# Build toolhead MCU firmware
./mcu_build.sh -b toolhead_gd32 -v 00.00.00.02 -f

# Build heated bed controller MCU firmware
./mcu_build.sh -b bed_sensor -v 00.00.00.02 -f

# Build options
-b Select target board configuration from mcu_config/
-v Select target firmware version (format: 00.00.00.00)
-f Build the bootloader and merge it into factory.bin
-m Merge bootloader.bin and app.bin into factory.bin
```
**Key Configuration Files**
- printer.cfg: Core printer settings (motion parameters, serial configuration, etc.)
- printer_dsp.cfg: Advanced DSP configuration
- product_test.cfg: Factory test configuration
- temperature_sensors.cfg: Temperature sensor calibration parameters

**Usage Examples**
```
# Start the application layer program
./elegoo_printer

# Start with a specific configuration
./elegoo_printer --config /path/to/config.cfg
```

**Development Guide**

**Code Structure**
- Modular Design: Each module is relatively independent of others
- Configuration-Driven: Behavior is controlled via configuration files
- Event-Driven: Uses a reactor pattern for handling asynchronous events
- Plugin Architecture: Supports extension modules
 
**Extension Development**
1. Add new feature modules under extras/
2. Modify CMakeLists.txt to include the new modules 
3. Update configuration files to support the new features
4. Implement the corresponding G-code processors
 
Note: This firmware is open source. Please thoroughly test it before deploying in production environments to ensure compatibility with your hardware and specified use cases.
