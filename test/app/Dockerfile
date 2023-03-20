# run docker file from the RFT root directory with:
# docker build -f test/app/Dockerfile -t rft .

# Note that an RMR image is required to build RFT
FROM rmr:ubuntu

ENV LD_LIBRARY_PATH=/usr/local/lib

COPY test/app/app.rt /tmp/

ENV RMR_SEED_RT=/tmp/app.rt
# export RMR_RTG_SVC=25321

# enabling warnings in RMR
# ENV RMR_LOG_VLEVEL=3

# ENV RFT_REPLICA_SERVERS=2
ENV RFT_REPLICATION_INTERVAL=10

# used to run RFT with this server_id rather than the container's host name
# ENV RFT_SELF_ID=xapp1

# Defines the xApp replication type
# use "all" for GLOBAL replication, or a positive number of servers for PARTIAL replication (default to 1)
# ENV RFT_REPLICA_SERVERS=all
# ENV RFT_REPLICA_SERVERS=2

WORKDIR /
COPY . /rft

WORKDIR /rft/test/app
RUN make
CMD './xapp2';'bash'
