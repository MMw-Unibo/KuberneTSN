#!/bin/bash

# Check if the script is being run as root
if [ $(id -u) -ne 0 ] ; then
    echo "Please run as root"
    exit 1
fi

# set -o xtrace

# Function to set container route
con_route() {
    CONTAINER=$1

    # Get container PID
    PID=`docker inspect -f '{{.State.Pid}}' "$CONTAINER"`

    # Create a link for the network namespace of the container
    ln -s /proc/"$PID"/ns/net /var/run/netns/"$PID"

    # Disable offload for the specified interface
    ip netns exec $PID ethtool --offload $PORTNAME rx off tx off

    # Delete the default route and add a new one with the specified gateway
    # ip netns exec $PID ip r del default 
    ip netns exec $PID ip r add default dev $PORTNAME

    # Remove the link when done
    rm -f /var/run/netns/"$PID"
}


# Function to add an OVS port to a container
add_ovs_port_to_container() {
    CONTAINER=$1
    echo "Adding OVS port to container $CONTAINER"

    # Add OVS port to container with specified IP address and gateway
    echo `ovs-docker add-port $SWITCH $PORTNAME $CONTAINER --ipaddress="$IP" 
    # --gateway="$GATEWAY"`

    # List OVS interfaces
    ovs-vsctl -- --columns=name,ofport list Interface
}

print_config() {
    echo "CONFIG_FILE=$CONFIG_FILE"
    echo "SWITCH=$SWITCH"
    echo "PORTNAME=$PORTNAME"
    echo "IP=$IP"
    echo "GATEWAY=$GATEWAY"
    echo "CONTAINER=$CONTAINER"
}

# Check options:
# -f: config file
# -h: help
while getopts "f:h" opt; do
    case $opt in
        f)
            CONFIG_FILE=$OPTARG
            ;;
        h)
            echo "Usage: $0 [-f config_file]"
            exit 0
            ;;
        \?)
            echo "Invalid option: -$OPTARG"
            exit 1
            ;;
    esac
done

if [ -z "$CONFIG_FILE" ]; then
    echo "Config file not set"
    exit 1
fi

source $CONFIG_FILE

print_config

# Check if the container is running
# if [ ! "$(docker ps -q -f name=$APP_CONTAINER)" ]; then
#     echo "Container $APP_CONTAINER is not running"
#     exit 1
# fi

# First add the OVS port to the container
add_ovs_port_to_container $CONTAINER

sleep 1

# then set the container route
con_route $CONTAINER