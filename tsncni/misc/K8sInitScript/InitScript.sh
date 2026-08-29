#!/bin/bash
set -euo pipefail

# ========================
# Parametri configurazione
# ========================
# Interfaccia fisica da preparare per DPDK
IFACE="enp23s0np0"
# Indirizzo PCI della scheda
PCI_ADDR="0000:17:00.0"
# Numero di HugePages da riservare
HUGE_PAGES=2048
# Dimensione delle HugePages
HUGE_PAGE_SIZE="2M"
# Nome del bridge interno OVS
BR_INT="br-int"
# Nome del bridge fisico OVS
BR_PHY="br-phy"
# Parametri VXLAN
VXLAN_REMOTE_IP="10.0.10.12"
VXLAN_LOCAL_IP="10.0.10.11"
VXLAN_DST_PORT=4789
VXLAN_KEY=500

export PATH=$PATH:/usr/local/share/openvswitch/scripts
export DB_SOCK=/usr/local/var/run/openvswitch/db.sock

# Configurazione HugePages
sysctl -w vm.nr_hugepages=$HUGE_PAGES
mkdir -p /mnt/hugepages
mount -t hugetlbfs none /mnt/hugepages -o pagesize=$HUGE_PAGE_SIZE || true
echo "Configured nr_hugepages = $(grep HugePages_Total /proc/meminfo)"

# Abilita VFIO no-IOMMU per il binding
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode


# Avvio Open vSwitch con DPDK
ovs-ctl --no-ovsdb-server --db-sock="$DB_SOCK" start
ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-init=true


# Configurazione DPDK memory
ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-socket-mem="1024"
echo "DPDK initialized? $(ovs-vsctl get Open_vSwitch . dpdk_initialized)"

# Preparazione interfaccia fisica per DPDK
ip addr flush dev "$IFACE"
ip link set dev "$IFACE" down

# Binding DPDK della scheda PCI corrispondente (non necessario per alcune schede es. Mellanox)
/usr/bin/python3 /usr/bin/dpdk-devbind.py --bind=vfio-pci "$PCI_ADDR"
/usr/bin/python3 /usr/bin/dpdk-devbind.py --status

# Ricrea i bridge OVS e pulisci flussi
for BR in "$BR_INT" "$BR_PHY"; do
  ovs-ofctl del-flows "$BR" 2>/dev/null || true
  ovs-vsctl del-br "$BR"       2>/dev/null || true
done

ovs-vsctl add-br "$BR_INT" -- set Bridge "$BR_INT" datapath_type=netdev
ovs-vsctl add-br "$BR_PHY" -- set Bridge "$BR_PHY" datapath_type=netdev

# Aggiungi porte e configurazioni TSN/DPDK
ovs-vsctl add-port "$BR_INT" vxlan0 \
    -- set interface vxlan0 type=vxlan \
       options:remote_ip=$VXLAN_REMOTE_IP options:local_ip=$VXLAN_LOCAL_IP \
       options:dst_port=$VXLAN_DST_PORT options:key=$VXLAN_KEY

ovs-vsctl add-port "$BR_PHY" dpdk0 \
    -- set Interface dpdk0 type=dpdk \
       options:dpdk-devargs="$PCI_ADDR"

ovs-vsctl add-port "$BR_INT" vhost-user0 \
    -- set Interface vhost-user0 type=dpdkvhostuser

# Imposta indirizzi IP e porta up
ip addr add $VXLAN_LOCAL_IP/24 dev "$BR_PHY"
ip link set dev "$BR_PHY" up
ip link set dev "$BR_INT" up

# Print stato finale
grep Huge /proc/meminfo

echo "=== OVS Bridges ==="
ovs-vsctl show
echo

echo "=== OVS Flows $BR_INT ==="
ovs-ofctl dump-flows "$BR_INT"
echo

echo "=== OVS Flows $BR_PHY ==="
ovs-ofctl dump-flows "$BR_PHY"

