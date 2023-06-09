# Use Alpine Linux as the base image for the first step
FROM ubuntu:latest AS build-env

# Install build dependencies
RUN apt-get update -y
RUN apt-get install -y numactl iproute2 build-essential vim pciutils libdpdk-dev dpdk dpdk-dev libibverbs-dev librdmacm-dev

# Set the working directory
WORKDIR /kubetsn

# Copy the source code to the container
COPY . .

# Compile the source code
RUN ./build.sh build release

# Use Alpine Linux as the base image for the second step
FROM ubuntu:latest

RUN apt-get update -y
RUN apt-get install -y dpdk libibverbs1

# Set the working directory
WORKDIR /kubetsn

# Copy the compiled binary from the first step
COPY --from=build-env /kubetsn/bin/ktsnd .

# Set the command to run when the container starts
ENTRYPOINT ["./ktsnd", "--single-file-segments", "--file-prefix=container", "--no-pci", "--vdev=virtio_user0,mac=00:00:00:00:00:01,path=/var/run/openvswitch/vhost-user0"]