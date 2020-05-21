# pgagroal

`pgagroal` is a high-performance protocol-native connection pool for [PostgreSQL](https://www.postgresql.org).

Pronounced: p-g-a-gro-al, named after [A](https://www.visitportugal.com/en/content/praia-fluvial-do-agroal)[gro](https://www.google.com/maps/place/Agroal,+Portugal/@39.6775431,-8.4486056,14z/)[al](https://www.infatima.pt/en/nearby/sun-sea/fluvial-beaches/agroal/) in Portugal.

## Features

* High performance
* Connection pool
* Limit connections for users and databases
* Prefill support
* Remove idle connections
* Perform connection validation
* Enable / disable database access
* Graceful / fast shutdown
* Prometheus support
* Remote management
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

See [Getting Started](./doc/GETTING_STARTED.md) on how to get started with `pgagroal`.

See [Configuration](./doc/CONFIGURATION.md) on how to configure `pgagroal`.

See [Performance](./doc/PERFORMANCE.md) for a performance run.

## Overview

`pgagroal` makes use of

* Process model
* Shared memory model across processes
* [libev](http://software.schmorp.de/pkg/libev.html) for fast network interactions
* [Atomic operations](https://en.cppreference.com/w/c/atomic) are used to keep track of state
* The [PostgreSQL](https://www.postgresql.org) native protocol
  [v3](https://www.postgresql.org/docs/11/protocol-message-formats.html) for its communication

See [Architecture](./doc/ARCHITECTURE.md) for the architecture of `pgagroal`.

## Tested platforms

* [Fedora](https://getfedora.org/) 28+
* [RHEL](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux) 7.x with
  [EPEL](https://access.redhat.com/solutions/3358) and
  [DevTools](https://developers.redhat.com/products/developertoolset/overview) 8+
* [RHEL](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux) 8.x with
  [AppStream](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/installing_managing_and_removing_user-space_components/using-appstream_using-appstream)

## Compiling the source

`pgagroal` requires

* [gcc 8+](https://gcc.gnu.org) (C17)
* [cmake](https://cmake.org)
* [make](https://www.gnu.org/software/make/)
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [rst2man](https://docutils.sourceforge.io/)

```sh
dnf install gcc cmake make libev libev-devel openssl openssl-devel python3-docutils
```

Alternative [clang 8+](https://clang.llvm.org/) can be used.

### Release build

The following commands will install `pgagroal` in the `/usr/local` hierarchy
and run the default configuration.

```sh
git clone https://github.com/agroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
/usr/local/bin/pgagroal -c /usr/local/etc/pgagroal/pgagroal.conf -a /usr/local/etc/pgagroal/pgagroal_hba.conf
```

See [RPM](./doc/RPM.md) for how to build a RPM of `pgagroal`.

### Debug build

The following commands will create a `DEBUG` version of `pgagroal`.

```sh
git clone https://github.com/agroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
cd src
cp ../../doc/etc/*.conf .
./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

Remember to set the `log_level` configuration option to `debug5`.

## Contributing

Contributions to `pgagroal` are managed on [GitHub.com](https://github.com/agroal/pgagroal/)

* [Ask a question](https://github.com/agroal/pgagroal/issues)
* [Raise an issue](https://github.com/agroal/pgagroal/issues)
* [Feature request](https://github.com/agroal/pgagroal/issues)
* [Code submission](https://github.com/agroal/pgagroal/pulls)

Contributions are most welcome !

Consider giving the project a [star](https://github.com/agroal/pgagroal/stargazers) on
[GitHub](https://github.com/agroal/pgagroal/) if you find it useful. And, feel free to follow
the project on [Twitter](https://twitter.com/pgagroal/) as well.

## License

[BSD-3-Clause](https://opensource.org/licenses/BSD-3-Clause)
