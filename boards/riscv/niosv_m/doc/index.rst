.. _niosv_m:

INTEL FPGA niosv_m
####################

Overview
********

niosv_m board is based on Nios® V/m Hello World Example Design system and this complete system is consisted of following IP blocks:

.. code-block:: console

	Nios® V/m Processor Intel® FPGA IP
	JTAG UART Intel® FPGA IP
	On-Chip Memory Intel® FPGA IP

Generate hello world example design
===================================

Please follow Nios® V/m processor example design section in Nios® V Embedded Processor Design Handbook to generate and build Nios® V/m processor example design system.
Generated SRAM Object File .sof file will be used to create the Nios® V/m processor inside the FPGA device.

Create Nios® V/m processor example design system
================================================

To program Nios® V/m processor based system into the FPGA and to run your application, use Intel Quartus Programmer tool.

To create the Nios® V/m processor inside the FPGA device, download the generated .sof file onto the board with the following command.

.. code-block:: console

	quartus_pgm -c 1 -m JTAG -o "p;top.sof@1"

.. code-block:: console

	Note:
	-c 1 is referring to JTAG cable number connected to the Host Computer.
	@1 is referring to device index on the JTAG Chain and may differ for your board.
	top.sof is referring to Nios® V/m processor based system SRAM Object File.

Download Zephyr elf file and run application
============================================

To download the Zephyr Executable and Linkable Format .elf file using the niosv-download command within Nios V Command Shell environment.

.. code-block:: console

	niosv-download -g <elf file>

Use the JTAG UART terminal to print the stdout and stderr of the Nios® V/m processor system.

.. code-block:: console

	juart-terminal

You should see message similar to the following in the JTAG UART terminal:

.. code-block:: console

	*** Booting Zephyr OS build zephyr-vn.n.nn  ***
	Hello World! niosv_m
