# KuberneTSN: containerized TSN scheduler for Kubernetes overlay networks 

| :warning: WARNING: _This is a work in progress_                                                                                                                                                                                                                                                        |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| The current version of KuberneTSN is a work in progress, and it's only a proof of concept implemented for containers running using Docker. The actual integration as a Multus CNI plugin is still work in progress, but it will mostly consist in the automation of the steps described in this guide. |

**Abstract**: The emerging paradigm of resource disaggregation enables the deployment of cloud-like services across a pool of physical and virtualized resources, interconnected using a network fabric. This design embodies several benefits in terms of resource efficiency and cost-effectiveness, service elasticity and adaptability, etc. Application domains benefiting from such a trend include cyber-physical systems (CPS), tactile internet, 5G networks and beyond, or mixed reality applications, all generally embodying heterogeneous Quality of Service (QoS) requirements. In this context, a key enabling factor to fully support those mixed-criticality scenarios will be the network and the system-level support for time-sensitive communication. Although a lot of work has been conducted on devising efficient orchestration and CPU scheduling strategies, the networking aspects of performance-critical components remain largely unstudied. Bridging this gap, we propose KuberneTSN, an original solution built on the Kubernetes platform, providing support for time-sensitive traffic to unmodified application binaries. We define an architecture for an accelerated and deterministic overlay network, which includes kernel-bypassing networking features as well as a novel userspace packet scheduler compliant with the Time-Sensitive Networking (TSN) standard. The solution is implemented as tsn-cni, a Kubernetes network plugin that can coexist alongside popular alternatives. To assess the validity of the approach, we conduct an experimental analysis on a real distributed testbed, demonstrating that KuberneTSN enables applications to easily meet deterministic deadlines, provides the same guarantees of bare-metal deployments, and outperforms overlay networks built using the Flannel plugin.

Please cite our work if you use KuberneTSN in your research:

```bibtex
@article{kubernetsn,
  title={KuberneTSN: a Deterministic Overlay Network for Time-Sensitive Containerized Environments},
  author={Garbugli, Andrea and Rosa, Lorenzo and Bujari, Armir and Foschini, Luca},
  journal={arXiv preprint arXiv:2302.08398},
  year={2023}
}
```

## Requirements

To run KuberneTSN you need a fresh installation of OVS-DPDK and Docker.

- For example, to install Docker on Ubuntu follow the instructions [here](https://docs.docker.com/engine/install/ubuntu/).
- To install OVS-DPDK follow the instructions [here](https://docs.openvswitch.org/en/latest/intro/install/dpdk/) or on Ubuntu you can install it directly using apt with the following command:

```bash
sudo apt install openvswitch-switch-dpdk
```

## Setup OVS-DPDK

To setup OVS-DPDK, first we need to bind the network interfaces to the DPDK driver.

First we must allocate the hugepages.

Then, we can check the available network interfaces with the following command:

```bash
sudo dpdk-devbind -s
```

Suppose we have the following output:

```bash
Network devices using DPDK-compatible driver
============================================

Network devices using kernel driver
===================================
0000:05:00.0 'Ethernet Controller I225-LM 15f2' drv=igc unused=vfio-pci,uio_pci_generic
```

For example, we can see a network interface `enp5s0` bounded to the `igc` driver. We can unbind it with the following commands:

Setting the interface down:

```bash
ip addr flush dev enp5s0
ip link set dev enp5s0 down
```

Binding the interface to the `vfio-pci` driver:

```bash
sudo dpdk-devbind -b=vfio-pci 0000:05:00.0
```

| :exclamation: NOTE: If you need to use the `vfio-pci` driver, you may need to load the vfio-pci module first, `sudo modprobe vfio-pci`, and enable unsafe mode: `echo 1 \| sudo tee /sys/module/vfio/parameters/enable_unsafe_noiommu_mode`. |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |


| :exclamation: NOTE: We tested our solution with OVS version 3.0.1 and DPDK version 21.11.2. Other versions will likely work as well, but they might require small configuration tweaks. |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |


### Setup the network

We have the following topology:

```
+------------------------------------+       +------------------+
| Host1                              |       | Host2            |
|                                    |       |                  |
|   +------------+    +------------+ |       |  +------------+  |
|   |    talker  |    |    ktsnd   | |       |  |  listener  |  |
|   | (container)|    | (container)| |       |  | (container)|  |
|   |------------|    |------------| |       |  |------------|  |
|   |192.168.1.11|    |            | |       |  |192.168.1.12|  |
|   |     veth   |    |vhost-user0 | |       |  |   veth     |  |
|   +------------+    +------------+ |       |  +------------+  |
|          |              |          |       |        |         |
|     +---------------------+        |       |  +------------+  |
|     |       br-int        |        |       |  |   br-int   |  |
|     |---------------------|        |       |  |------------|  |
|     |       vxlan0        |        |       |  |   vxlan0   |  |
|     +---------------------+        |       |  +------------+  |
|                |                   |       |         |        |
|                |                   |       |         |        |
|           +------------+           |       |  +------------+  |
|           |   br-phy   |           |       |  |   br-phy   |  |
|           | 10.0.10.11 |           |       |  | 10.0.10.12 |  |
|           |            |           |       |  |            |  |
|           |------------|           |       |  |------------|  |
|           |    dpdk0   |           |       |  |    dpdk0   |  |
|           +------------+           |       |  +------------+  |
|                 |                  |       |        |         |
|           |------------|           |       |  |------------|  |
|-----------+   pNIC     +-----------+       +--+   pNIC     +--+
            |------------|                      |------------|
                  |                                   |
                  |                                   |
                  |-----------------------------------|
```

To setup the network we need to create the following bridges:

- `br-int` - the integration bridge
- `br-phy` - the physical bridge

we can use the following commands:

```bash
sudo ovs-vsctl add-br br-int -- set Bridge br-phy datapath_type=netdev
sudo ovs-vsctl add-br br-phy -- set Bridge br-phy datapath_type=netdev
```

Then we add the IP addresses to the bridges:

```bash
sudo ip addr add 10.0.10.12/24 dev br-phy
sudo ip link set dev br-phy up
sudo ip link set dev br-int up
```

Then we need to create the following ports:

```bash
sudo ovs-vsctl add-port br-int vxlan0 -- set interface vxlan0 type=vxlan options:remote_ip=10.0.10.12
sudo ovs-vsctl add-port br-phy dpdk0 -- set Interface dpdk0 type=dpdk options:dpdk-devargs=0000:05:00.0
sudo ovs-vsctl add-port br-phy vhost-user0 -- set Interface vhost-user0 type=dpdkvhostuser
```

The `veth` interfaces are set up during the container configuration, in the script `scripts/kubetsn.sh`.

## Build and run the TSN deamon scheduler and Perf application

To build the image for TSN deamon scheduler run:

```bash
docker image build --network=host -t kubetsn/ktsnd .
```

Then run the container with:

```bash
docker run --cpuset-cpus 1 -d --privileged --net=none --name ktsnd -v /dev/shm:/dev/shm -v /dev/hugepages:/dev/hugepages -v /usr/local/var/run/openvswitch:/var/run/openvswitch kubetsn/ktsnd
```

The arguments of the run command are:

- `--cpuset-cpus 1` - run the container on the second core
- `-d` - run the container in the background
- `--privileged` - run the container in the privileged mode
- `--net=none` - do not create a network interface for the container
- `-v /dev/shm:/dev/shm` - mount the shared memory used by the TSN scheduler to "receive" the packets from applications
- `-v /dev/hugepages:/dev/hugepages` - mount the hugepages needed by the DPDK
- `-v /usr/local/var/run/openvswitch:/var/run/openvswitch` - mount the OVS socket used by the DPDK as network interface

When start the container, the TSN scheduler is started automatically using the following command:

```bash
./ktsnd --single-file-segments --file-prefix=container --no-pci --vdev=virtio_user0,mac=00:00:00:00:00:01,path=/var/run/openvswitch/vhost-user0
```

To build the image for TSN Perf application run:

```bash
docker image build --network=host -f Dockerfile.perf -t kubetsn/tsn-perf .
```

Then run the container with:

```bash
docker run --cpuset-cpus 2 --net=none --name tsn-app -it -v /dev/shm:/dev/shm kubetsn/tsn-perf /bin/bash
```

The arguments of the run command are:

- `--cpuset-cpus 2` - run the container on the third core
- `--net=none` - do not create a network interface for the container
- `-v /dev/shm:/dev/shm` - mount the shared memory used by the application to "send" the packets with the TSN scheduler

Before running the application, we must attach an OVS interface to the container. To do that, run the following command:

```bash
sudo ./scripts/kubetsn.sh -f ./scripts/talker # in case of the talker application
# or
sudo ./scripts/kubetsn.sh -f ./scripts/listener # in case of the listener application
```

Then attach to the container with:

```bash
docker attach tsn-app
```

and run the application with:

```bash
# for the talker application
APP_ROLE=talker LD_PRELOAD=./libktsn.so ./tsn-perf -a 192.168.100.12 -t -v

# for the listener application
APP_ROLE=listener ./tsn-perf -a 192.168.100.12 -p 9999 -v
```
