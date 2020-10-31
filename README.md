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

The library can be linked with any xApp that wants
fault tolerance support in a similar manner as linking the xApp
core with the RMR messaging library and other libraries such as SDL
and logging.

## Getting Started

### Prerequisites

```
gcc make git
```

The [RIC Message Router (RMR)](https://wiki.o-ran-sc.org/pages/viewpage.action?pageId=3605041) is also required as the underlying transport mechanism to exchange messages between the RFT nodes.

### Building and Installing

Please, follow these [instructions](https://wiki.o-ran-sc.org/display/RICP/RMR+Building+From+Source) to build and install the RMR dependency before the RFT.

Then, compile and install the development version of the RFT following these steps:
```
make
sudo make install
```
See the [BUILD](BUILD) file for specific instructions to build the RFT in production systems.

### Linking your application

The RFT library can be linked with the -lrft option using the C compiler. However, it also requires linking with other dependencies as follows:
```
-lrmr_si -lpthread -lm -lrft
```

### Running tests

The RFT project provides unit tests under the [test/unit](test/unit) directory. The [CppUTest](https://cpputest.github.io/) framework is employed to write unit tests. Even though RFT does not yet provide full unit test coverage, new tests will be written on updating and adding features to the RFT. Eventually, the RFT will reach full unit test coverage.

Contributions to the development of unit tests are really welcome!

## Using the RFT API

The RFT provides a clean and lightweight, but powerful API to the developers that want to add fault tolerance features to their xApps. The main functions that an xApp developer has to call from the RFT library are:

* rft_init( mrc, port, apply_state_cb, take_snapshot_cb, install_snapshot_cb ) - initializes the RFT library passing the RMR context and port, as well as registers three callback functions to the xApp state machine

* rft_enqueue_msg( msg ) - pass a message to the RFT library. A set of messages are reserved to the RFT and xApp only needs to forward that messages to the RFT by using this function

* rft_replicate( command, context, key, value, len ) - operation called by the xApp to replicate its state passing the following arguments:

    * command: the command (int) which will be executed by the state machine
    * context: defines the ID of a RAN (Radio Access Network) element on which the state will be modified by the command (i.e. UE ID, Cell ID, Global ID)
    * key: defines which key of the context the command should apply the value (e.g. device counter, traffic class)
    * value: holds the value that the command should compute and apply
    * len: holds the size of the value

* apply_state_cb( command, context, key, value, len ) - calls the command of the finite state machine. This function needs to be implemented by the xApp developer and it will be called by the RFT library (call back) when a command needs to be applied on the replicated state machine.

* take_snapshot_cb( contexts, nctx, items, data ) - calls the function to serialize all primary contexts at the xApp replica.

* install_snapshot_cb( items, data ) - calls the function do desserialize the received snapshot at the xApp replica.

## Examples

See some xApp examples using the RFT library in the [examples](examples) directory.

## Contributing

Everyone is welcome to contribute to this project.

You can get the latest development version at the **develop** branch.

## Versioning

This RFT project is based on its previous alpha releases 0.1, 0.2, 0.3, and 0.4. The original project was reorganized in the form of a library to allow programmers to easily link the RFT into their applications to provide fault tolerance.

Given a version number MAJOR.MINOR.PATCH, it should be incremented as follows:

* MAJOR - when you make incompatible API changes,
* MINOR - when you add functionality in a backward-compatible manner, and
* PATCH - when you make backward-compatible bug fixes.

For the versions available, please see the tags on this repository.

## Authors

* **Alexandre Huff** <<alexandrehuff@gmail.com>> - Initial developer and maintainer

* See also the list of **contributors** who participated in this project.

Please, contact the initial maintainer of the project for further information or get access to the previous commit history.

## Licenses

This project is licensed under the Apache License and Creative Commons License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* Thanks to **Matti Hiltunen** from AT&T Labs - Research for guiding this project.
* Thanks to **Edward Scott Daniels** from AT&T Labs - Research for the valuable discussions.
* Thanks to **Elias P. Duarte Jr.** from the Federal University of Parana.
* The logger module was inspired by the [Macro Logger](https://github.com/dmcrodrigues/macro-logger) project.
