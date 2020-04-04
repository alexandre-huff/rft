# RIC Fault Tolerance - RFT

RFT is a project which implements a library that can be used
to provide fault-tolerant features for xApps.

xApp fault tolerance can be achieved by state replication
techniques that rely on partial asynchronous replication
among groups of xApps. Full asynchronous replication is also
implemented, which all xApp instances eventually will have an
identical copy of the global state of the application.

A policy-based message routing strategy ensures that
messages from the RAN elements are delivered to the right
xApp replica that maintains the state needed to process that
message. The other xApp instances act as replicas of that state.
As a result, the consensus is only needed for membership
management.

A Raft-based membership algorithm is implemented to
maintain both group membership and work assignments.
The leader is in charge of the xApp work assignments and
for updating the routing rules based on those work assignments.

This library can be linked with any xApp that wants
fault-tolerance support in a similar manner as linking the xApp
core with the RMR messaging library and other libraries such as SDL
and logging.
