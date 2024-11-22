# How to Set Up and Use Open vSwitch with DPDK

Open vSwitch (OVS) is a popular open-source software switch that can run on any Linux-based system. It supports various features such as VLANs, tunneling, traffic shaping, and network virtualization. OVS can also leverage the Data Plane Development Kit (DPDK) to improve its performance and efficiency.

DPDK is a set of libraries and drivers that enable fast packet processing on x86 platforms. It allows applications to bypass the kernel and access the network interface directly, reducing latency and CPU overhead.

In this blog post, we will show you how to set up and use OVS with DPDK on a Linux system. We will cover the following steps:

- Install OVS and DPDK
- Configure huge pages and bind network interfaces to DPDK
- Start OVS with DPDK support
- Create a virtual switch and ports using the DPDK datapath
- Test the connectivity and performance of the switch

Let's get started!

## Install OVS and DPDK

The first step is to install OVS and DPDK on your system. You can either use the packages provided by your distribution or compile them from source. In this example, we will use Ubuntu 20.04 and install OVS 3.0.1 and DPDK 21.11.2 from source.

To install OVS and DPDK from source, follow these steps

1. Install the dependencies

```sh
$ sudo apt update
$ sudo apt install build-essential git autoconf libtool openssl libssl-dev python3-pip python3-setuptools python3-wheel ninja-build meson libnuma-dev
```

2. Clone the OVS repository and checkout the 3.0.1 branch:

```sh
$ git clone https://github.com/openvswitch/ovs.git
$ cd ovs
$ git checkout v3.0.1
```

3. Clone the DPDK repository and checkout the 21.11.2 branch:

```sh
$ git clone https://github.com/DPDK/dpdk.git
$ cd dpdk
$ git checkout v21.11.2
```

4. Build and install DPDK:

```sh
$ meson build
$ cd build
$ ninja -C build
$ sudo ninja -C build install
```

5. Build and install OVS with DPDK support:

```sh
$ ./boot.sh
$ ./configure --with-dpdk=$PWD/dpdk/build --prefix=/usr/local --localstatedir=/usr/local/var --sysconfdir=/usr/local/etc --enable-Werror
$ make -j$(nproc)
$ sudo make install
```

6. Add the OVS scripts directory to your PATH:

```sh
$ export PATH=$PATH:/usr/local/share/openvswitch/scripts
```

## Configure huge pages and bind network interfaces to DPDK

The next step is to configure huge pages and bind network interfaces to DPDK. Huge pages are large memory pages that can improve the performance of memory-intensive applications like DPDK. Binding network interfaces to DPDK means assigning them to a specific driver that allows them to be used by DPDK applications.

To configure huge pages and bind network interfaces to DPDK, follow these steps:

1. Check the number of huge pages and their size:

```sh
$ cat /proc/meminfo | grep --color Huge
# or
$ sysctl vm.nr_hugepages
```

2. To allocate more huge pages, edit the `/etc/default/grub` file and add the following parameter to the `GRUB_CMDLINE_LINUX_DEFAULT` line:

```sh
hugepagesz=1G hugepages=4 default_hugepagesz=1G
```

This will allocate 4 huge pages of 1 GB each.

3. Update the grub configuration and reboot your system:

```sh
$ sudo update-grub
$ sudo reboot
```

4. After rebooting, verify that the huge pages are allocated:

```sh
$ cat /proc/meminfo | grep --color Huge
# or
$ sysctl vm.nr_hugepages
```

5. To bind a network interface to DPDK, you need to know its PCI address and driver name. You can use the `lspci` command to list all PCI devices on your system:

```sh
$ lspci | grep "Ethernet controller"
```

6. To bind an interface to DPDK, you can use the `dpdk-devbind.py` script that comes with DPDK. For example, to bind an interface with PCI address `0000:05:00.0` to the `vfio-pci` driver, run:

> NOTE: If you need to use the `vfio-pci` driver, you may need to load the vfio-pci module first, `sudo modprobe vfio-pci`, and enable unsafe mode: `echo 1 | sudo tee /sys/module/vfio/parameters/enable_unsafe_noiommu_mode`

```sh
# To unbind the interface from the current driver
# First, flush the interface and set it down
sudo ip a flush dev enp5s0
sudo ip link set dev enp5s0 down

# example: dpdk-devbind.py -b=vfio-pci 0000:05:00.0
dpdk-devbind.py -b=<driver> <pci>

# To check the current status
dpdk-devbind.py -s
```

7. Repeat this step for any other interface you want to bind to DPDK.

## Start OVS with DPDK support

The next step is to start OVS with DPDK support. To do so, follow these steps:

1. Add the OVS scripts directory to your PATH:

```sh
$ export PATH=$PATH:/usr/local/share/openvswitch/scripts
```     

2. Start the OVS database server and vswitchd:

```sh
$ ovs-ctl --no-ovs-vswitchd start
$ export DB_SOCK=/usr/local/var/run/openvswitch/db.sock
$ ovs-ctl --no-ovsdb-server --db-sock="$DB_SOCK" start
```

3. Set Open vSwitch to use DPDK and configure the DPDK environment:
```sh
$ ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-init=true

# Check the config
$ ovs-vsctl --no-wait get Open_vSwitch . other_config:dpdk-init
"true"
# or
$ ovs-vsctl get Open_vSwitch . dpdk_initialized
true

$ ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-socket-mem="1024"

# Set the core used by the DPDK lcore (polling thread)
$ ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-lcore-mask="0x1"

# To check the DPDK version used
$ ovs-vsctl get Open_vSwitch . dpdk_version
"DPDK 21.11.2"
```

## Create a virtual switch and ports using the DPDK datapath

The next step is to create a virtual switch and ports using the DPDK datapath. A virtual switch is a software abstraction that acts like a physical switch, connecting multiple virtual or physical ports together. A port is an endpoint of a connection that can be either a physical network interface or a virtual interface.

To create a virtual switch and ports using the DPDK datapath, follow these steps:

1. Create a virtual switch named `br0` that uses the `netdev` datapath type:

```sh
$ ovs-vsctl add-br br0 -- set bridge br0 datapath_type=netdev

# Check the result:
$ ovs-vsctl show

# You should see something like this:
f264ddac-9553-47b8-8f2a-625db5e5241e
    Bridge br0
        datapath_type: netdev
        Port br0
            Interface br0
                type: internal
    ovs_version: "3.0.1"
```

2. Add a virtual port named `dpdk-p0` that uses the `dpdk` interface type and binds to a physical interface with PCI address `0000:05:00.0`:

```sh
# example: ovs-vsctl add-port br0 dpdk-p0 -- set Interface dpdk-p0 type=dpdk options:dpdk-devargs=0000:05:00.0
$ ovs-vsctl add-port br0 <port_name> -- set Interface <port_name> type=dpdk options:dpdk-devargs=<pci_address>

# Check the result:
$ ovs-vsctl show
92ec8a1a-4818-433c-9bcb-18f8f740301f
    Bridge br0
        datapath_type: netdev
        Port dpdk-p0
            Interface dpdk-p0
                type: dpdk
                options: {dpdk-devargs="0000:05:00.0"}
        Port br0
            Interface br0
                type: internal
    ovs_version: "3.0.1"
```

We can delete the port using:
```sh
$ ovs-vsctl del-port br0 dpdk-p0

# or

$ ovs-vsctl del-port dpdk-p0
```

## Add connection between two hosts

We have the following topology:

```
+------------------+                    +------------------+
| Host1            |                    | Host2            |
|                  |                    |                  |
|  +------------+  |                    |  +------------+  |
|  |  br0       |  |                    |  |  br0       |  |
|  | 10.0.11.1  |  |                    |  | 10.0.12.0  |  |
|  |            |  |                    |  |            |  |
|  |------------|  |                    |  |------------|  |
|  |  dpdk-p0   |  |                    |  |  dpdk-p0   |  |
|  +------------+  |                    |  +------------+  |
|        |         |                    |        |         |
|  |------------|  |                    |  |------------|  |
|--+   pNIC     +--+                    +--+   pNIC     +--+
   |------------|                          |------------|
        |                                       |
        |                                       |
        |---------------------------------------|
```

1. We can add ip addresses to the bridges using:

```sh
$ ip link set dev br0 up
$ ip a add 10.0.11.1/24 dev br0
```

2. We can add a route to host using:

```sh
$ ip r add 10.0.12.0/24 via 10.0.11.1 dev br0
```

3. Finally, test the connection using `ping`. Make sure that packet forwarding is enabled on the hosts:

```sh
$ sysctl -w net.ipv4.ip_forward=1
```