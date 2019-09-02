ftrap
=====

[![Release][release-badge]][release-url]
[![Build Status][travis-badge]][travis-url]
[![Boost License][license-badge]][license-url]

ftrap is a simple linux utility that invokes a command and sends SIGHUP to the
command when specified files are changed. Usage:

```console
$ ftrap -f FILE COMMAND ...
```

## Install

Download the latest static build from the [release page][release-url] or build
your own one by `make`.

## Test

```console
$ make
$ tests/run
```

## License

Boost License.

[release-badge]: https://img.shields.io/github/release/snsinfu/ftrap.svg
[release-url]: https://github.com/snsinfu/ftrap/releases
[travis-badge]: https://travis-ci.org/snsinfu/ftrap.svg?branch=master
[travis-url]: https://travis-ci.org/snsinfu/ftrap
[license-badge]: https://img.shields.io/badge/license-Boost-blue.svg
[license-url]: https://github.com/snsinfu/ftrap/blob/master/LICENSE.txt
