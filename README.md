# TraceTogether Clone Project

The main objective of this project is to design a system that is able to detect the duration(s) for which two IoT devices are observed to be in proximity, 
where proximity is defined to be a distance of <3m. This function is similar to that of the [TraceTogether smartphone app](https://www.tracetogether.gov.sg/) 
released by the Singaporean Government that allows for pandemic contact tracing using the BLE radio.

The [CC2650 SensorTag](https://www.ti.com/tool/TIDC-CC2650STK-SENSORTAG) device was used for the programming of this system.

### The following system output can be expected:

| Output                                                   | Meaning |
|----------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|
| "timestamp" DETECT "nodeID"                              | Device has detected another device with node ID  <nodeID> for the first time, and it has entered in proximity |
| "timestamp" LEAVE "nodeID"                               | Device has determined that the node with node ID  <nodeID> has moved away from proximity for 30s               |
| "timestamp" !! CLOSE PROXIMITY FOR 30S !! NODE: "nodeID" | Device has detected another device with node ID <nodeID> in proximity for 30s                                 |
| Node "nodeID" CONTACT TIME: "x seconds"                  | Device has a contact time of x seconds with the node with <nodeID>, prior to its departure from proximity |

## Compilation Steps
The following steps are relevant to the macOS operating system:
1. Ensure that you have the [contiki repository](https://github.com/contiki-os/contiki) on your local machine
2. Clone this repository locally into the `contiki/examples` folder
3. Run the following command in the root of the repository: `make TARGET=srf06-cc26xx BOARD=sensortag/cc2650 nbr_discovery.bin CPU_FAMILY=cc26xx`
4. Flash the generated `nbr_discovery.bin` onto your CC2650 SensorTag device using the [Uniflash](https://www.ti.com/tool/UNIFLASH) software provided by Texas Instruments
5. Observe the generated output by running the following command in the parent of the `contiki` folder: `contiki/tools/sky/serialdump-macos -b115200 /dev/tty.usbmodem<sensortag's USB name>`. Your SensorTag's USB name can be found by executing the following command: `ls /dev/tty*`

## System Background
The OS to be used is Contiki (http://www.contiki-os.org/). Contiki is an open source operating system, initially developed by Adam Dunkels (http://dunkels.com/adam/ ). Now, Contiki is supported by a large community of developers. Apart from for supporting both IPv4 and IPv6, it also includes multiple low-power wireless standards. Moreover, it runs on multiple low-power devices (http://www.contiki-os.org/hardware.html ), where the SensorTag is one of them.
