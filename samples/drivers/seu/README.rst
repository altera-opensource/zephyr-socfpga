.. _seu_sample:

Single Event Upsets(SEU) sample
##############

Overview
********

The SEU subsystem is meticulously crafted to fulfill a dual purpose within its
operational framework. Its primary function lies in promptly detecting and
reporting single event upsets errors to users. Additionally, this subsystem
offers a streamlined mechanism for the deliberate insertion of errors.

Building and Running
********************
For building the application:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/seu
   :board: intel_socfpga_agilex_socdk
   :goals: build

For running the application the Zephyr image can be loaded in DDR memory
at address 0x00100000 and ATF BL31 at 0x00001000 from SD Card or QSPI Flash
in ATF BL2.

Sample Output
*************
.. code-block:: console

*** Booting Zephyr OS build zephyr-v3.4.0-390-ga8fabc416394 ***
SEU Test Started
The Client No is 0x82bff6de
The Client No is 0x82cf4162
SEU Safe Error Insert Test Started
The SEU Error Type: 1:
The Sector Address: 5
The Correction status: 1
The row frame index: 6
The bit position: 16
SEU Safe Error Insert Test Completed
Read SEU Statistics Test Started
The value of t_seu_cycle : 0x7735942d
The value of t_seu_detect : 0xa7bba
The value of t_seu_correct : 0xab6
The value of t_seu_inject_detect : 0x6bf8c537
The value of t_sdm_seu_poll_interval : 0x61a80
The value of t_sdm_seu_pin_toggle_overhead : 0x2fd3
Read SEU Statistics Test Completed
Read ECC Error Test Started
The ECC Error Type: 1:
The Sector Address: 255
The Correction status: 1
Ram Id: 1
Read ECC Error Test Completed
SEU Test Completed
