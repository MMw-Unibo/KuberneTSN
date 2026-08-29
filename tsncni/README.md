# TSNCNI — Time-Sensitive Networking CNI Plugin for Kubernetes

## 1. Overview and Objectives

TSNCNI is a custom Container Network Interface (CNI) plugin designed to integrate Time-Sensitive Networking (TSN) capabilities into Kubernetes as part of the broader **KuberneTSN** project.  
The plugin employs Open vSwitch (OVS) as its primary data plane, enabling a controlled experimental comparison between deterministic TSN-enabled networking (OVS/DPDK) and conventional best-effort Kubernetes networking based on Flannel.

## 2. Repository Scope and Contributions

This repository encompasses:
- The implementation of the TSNCNI CNI plugin written in Go.
- Kubernetes manifests for deploying KuberneTSN and its associated components.
- Initialization scripts for preparing cluster nodes with TSN and OVS prerequisites.
- Python-based tools for processing and visualizing experimental results.
- A comprehensive dataset of performance measurements collected during experimental evaluations on both bare-metal and virtualized environments.

## 3. CNI Behavior and Network Model

The plugin strictly follows the CNI specification and implements the standard ADD and DEL operations, enabling seamless integration with kubelet and Multus.

### ADD Operation (Pod Creation)

When a Pod is created, TSNCNI performs the following steps:
1. Receives the CNI request from kubelet.
2. Invokes the `host-local` IPAM plugin to allocate an IP address, subnet, and gateway.
3. Creates a virtual Ethernet pair (veth):
   - One endpoint remains in the host network namespace.
   - The other endpoint is moved into the Pod’s network namespace.
4. Attaches the host-side veth interface to a pre-configured OVS bridge.
5. Configures IP addressing and the default gateway inside the Pod.
6. Returns the CNI result to Kubernetes.

### DEL Operation (Pod Deletion)

When a Pod is terminated, TSNCNI:
- Detaches the veth interface from the OVS bridge.
- Deletes the veth pair from the Pod’s network namespace.
- Notifies the IPAM plugin to release the allocated IP address.

### 3.1 Plugin Configuration

Configuration is provided through the CNI JSON configuration file and consumed at runtime by the plugin.

Key configuration parameters include:
- `OvsBridge`: the name of the OVS bridge to which Pod interfaces are connected.
- `ProvisionOvs`: a placeholder flag intended for future automatic OVS provisioning.

## 4. Kubernetes Manifests (`yaml/`)

### 4.1 Deployment Manifests (`yaml/deploy/`)

This directory includes:
- `ktsn-all.yml`: full deployment of KuberneTSN and TSNCNI  
  - Node 1: TSN talker + `ktsnd`  
  - Node 2: TSN listener
- `ktsn-double-daemon.yml`: alternative deployment using a dual TSN daemon configuration  
  - Node 1: TSN listener/talker + `ktsnd`  
  - Node 2: TSN listener/talker + `ktsnd`

### 4.2 NetworkAttachmentDefinitions (`yaml/nad/`)

These manifests define additional networks for Pods using Multus:
- `flannel-nad.yaml`: attaches Pods to a Flannel-based network.
- `tsncni-nad.yaml`: attaches Pods to the TSNCNI-managed TSN network (same subnet and IP range on both nodes).

Optional:
- `tsncni-nad-dual.yaml`: attaches Pods to the TSNCNI-managed TSN network using different IP ranges on each node.

## 5. Auxiliary Materials and Tools (`misc/`)

### 5.1 Container Images

`ContainerImages.txt` documents the OCI image repository storing all container images used in the experimental setup, including:
- `ktsnd` TSN daemon
- `ktsnd` TSN daemon DEBUG (compiled with debug enabled)
- TSN test application (application able to act as talker or listener)

### 5.2 Initialization Scripts

- `InitScript.sh`: prepares cluster nodes by configuring OVS, hugepages, and all parameters required for TSN and DPDK operation.
- `Sample_Init.txt`: example output demonstrating a successful node initialization.

### 5.3 Data Analysis and Visualization (`misc/ToolCalcAndPlot/`)

This directory contains Python tools for processing network performance logs:
- `main.py`: main analysis script computing latency and jitter metrics from raw logs.
- `listener_log.csv` and `talker_log.csv`: input datasets capturing per-packet timestamps.
- `duebox/duebox.py`: utility for generating comparative boxplots.
- Output files and images illustrate performance differences between TSN/DPDK and Flannel configurations.

## 6. Experimental Results (`tests/results/`)

Performance evaluation results are organized into two main categories.

### 6.1 Bare-Metal Experiments (`BM-UNI`)

These experiments were conducted on dedicated university infrastructure.  
Results are grouped by packet payload size:
- 64 bytes
- 512 bytes
- 1024 bytes

Each experiment includes:
- CSV logs containing per-packet latency measurements.
- Comparative boxplots illustrating latency and jitter distributions.
- Separate results for TSN/DPDK and Flannel configurations.

### 6.2 Virtual Machine Experiments

These experiments were executed on virtualized environments to evaluate:
- The impact of virtualization on TSN performance.
- Performance differences compared to bare-metal deployments.


The current implementation focuses on deterministic Layer-2 connectivity and does not yet provide:
- Automated OVS provisioning.
- Multi-hop TSN path.
These aspects are considered future work within the KuberneTSN project.


This repository accompanies a thesis in Computer Engineering and serves as:
- A reference implementation of a TSN-capable CNI plugin.
- The experimental platform used to collect and analyze all performance results discussed in the thesis.

## 📫 Contact

Email: andreab1081@gmail.com
