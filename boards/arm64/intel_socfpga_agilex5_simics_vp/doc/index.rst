.. _intel_socfpga_agilex5_simics_vp:

Intel Agilex® 5 E-Series Simics® Virtual Platform Universal
###########################################################

Simulator Overview
******************
Simics® is a full-system simulator supporting the definition, development, and
deployment of virtual platforms. It is fast, accurate, scalable and extensible.
Simics® simulator runs unchanged target binaries in a fast and controllable way, providing an
ideal environment for early software development and testing pre-silicon and post-silicon,
and even post-availability

Supported drivers
*****************
The Intel Agilex5 Simics® Virtual Platform models Quad core system with ARM CPU,
2 Cortex-A55 cores and 2 Cortex-A76 cores. Along with this, the following hardware subsystems
are also modeled.

+-----------+------------+---------------------------------------------+
| Interface | Controller | Hardware Subsystem Vendor                   |
+===========+============+=============================================+
| GIC-600   | on-chip    | ARM GICv3 interrupt controller              |
+-----------+------------+---------------------------------------------+
| UART      | on-chip    | NS16550 compatible serial port              |
+-----------+------------+---------------------------------------------+
| I2C       | on-chip    | Synopsys Designware                         |
+-----------+------------+---------------------------------------------+
| SD Host   | on-chip    | Cadence Design Systems                      |
+-----------+------------+---------------------------------------------+
| GPIO      | on-chip    | Intel Corporation                           |
+-----------+------------+---------------------------------------------+
| Timer     | on-chip    | Synopsys Designware                         |
+-----------+------------+---------------------------------------------+
| Watchdog  | on-chip    | Synopsys Designware                         |
+-----------+------------+---------------------------------------------+
| ARM TIMER | on-chip    | ARM system timer                            |
+-----------+------------+---------------------------------------------+
| Reset     | on-chip    | Intel Corporation, SoCFPGA Reset controller |
+-----------+------------+---------------------------------------------+
| Clock     | on-chip    | Intel Corporation, SoCFPGA Clock controller |
+-----------+------------+---------------------------------------------+

NOTE: As of now, the Zephyr configuration uses only a single core i.e. core0 Cortex-A55.

The default configuration can be found in the defconfig file:
        `boards/arm64/intel_socfpga_agilex_simics_vp/intel_socfpga_agilex5_simics_vp_defconfig`

Zephyr Boot Flow
****************
Zephyr image will need to be loaded by Intel Arm Trusted Firmware (ATF).
ATF BL2 is the First Stage Boot Loader (FSBL) and ATF BL31 is the Run time resident firmware which
provides services like SMC (Secure monitor calls) and PSCI (Power state coordination interface).

Boot flow:
        ATF BL2 (EL3) -> ATF BL31 (EL3) -> Zephyr (EL2->EL1)

Intel Arm Trusted Firmware (ATF) can be downloaded from github:
        `altera-opensource/arm-trusted-firmware <https://github.com/altera-opensource/arm-trusted-firmware.git>`_

Intel Zephyr project can be downloaded from github:
        `altera-opensource/zephyr-socfpga <https://github.com/altera-opensource/zephyr-socfpga>`_

How to run on Simics® Virtual Platform
**************************************
Simics® simulator also comes with a CLI (command line interface) that can be used to control
the flow of the simulation. For this, the Simics® simulator is provided with a set of commands
that the user can type into the CLI to interact with the simulation. Please refer to the Simics®
User Guide from References section for more details.

Example CLI:
        run

        run-command-file

        load-file

        log-level 4

In the Simics® file, we can include the necessary CLI commands and sequentially execute them.
A typical Simics® file example is as follows where it contains First Stage Boot Loader ATF BL2
information, and SD card image details which contains ATF BL31 and Zephyr images as one package
called fip.bin (Firmware Image Package binary).

Example sample.simics file:
    $create_hps_serial0_console=TRUE

    $create_hps_sd_card=TRUE

    $sd_image_filename = sd_image.img

    $fsbl_image_filename=<bl2.bin>

    run-command-file "targets/sm-hps/agilex5e-universal.simics"

    run

Simics® Debug Support
*********************
Simics® simulator supports the debugging of software using symbols files, C/C++ debugger, full
hardware inspection with all devices and register names, step-by-step execution, trace generation,
advanced breakpoints, and connection to remote debuggers such as GDB. Please refer to the Simics®
User Guide from References section for more details.

References
**********
`Simics® Simulator Public Release <https://www.intel.com/content/www/us/en/developer/articles/tool/simics-simulator.html>`_
