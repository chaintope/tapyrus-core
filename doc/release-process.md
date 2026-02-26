Release Process
====================

Before every release candidate:

* Update manpages, see [gen-manpages.sh](https://github.com/chaintope/tapyrus-core/blob/master/contrib/devtools/README.md#gen-manpagessh).

Before every minor and major release:

* Update version in `CMakeLists.txt` (don't forget to set `CLIENT_VERSION_IS_RELEASE` to `true`)
* Write release notes (see below)

Before every major release:

* Branch new branch like `v0.5` from `master` for future minor releases (minor releases like `v0.5.1` will be released from the version branch)
* Update `source-branch` in `snap/snapcraft.yml` to match the version branch (commit it directly to the version branch)
* Tag the version branch

### Reproducible Builds with Guix

Tapyrus Core uses [GNU Guix](https://guix.gnu.org) for reproducible builds. Guix provides a deterministic build environment that ensures the same binaries can be built independently by different parties.

First time setup:

1. Install Guix following the [installation instructions](https://guix.gnu.org/manual/en/html_node/Binary-Installation.html)
2. Clone the Tapyrus Core repository:

    git clone https://github.com/chaintope/tapyrus-core.git --recursive
    cd tapyrus-core

### Release Notes Guidelines

Write release notes. git shortlog helps a lot, for example:

    git shortlog --no-merges v(current version, e.g. 0.7.2)..v(new version, e.g. 0.8.0)


Generate list of authors:

    git log --format='- %aN' v(current version, e.g. 0.16.0)..v(new version, e.g. 0.16.1) | sort -fiu

Tag version (or release candidate) in git

    git tag -s v(new version, e.g. 0.8.0)

### Building Releases with Guix

Tapyrus Core uses CMake and Guix for building reproducible releases. The Guix build system ensures deterministic builds across different environments.

#### Prerequisites for macOS builds

For macOS cross-compilation, you need to extract the macOS SDK. See [contrib/macdeploy/README.md](../contrib/macdeploy/README.md#sdk-extraction) for detailed instructions.

Place the extracted SDK in `depends/SDKs/`:

    mkdir -p depends/SDKs
    cp path/to/extracted-SDK-file depends/SDKs/

#### Build Release Binaries

To build binaries for all supported platforms:

    # Build for Linux
    ./contrib/guix/guix-build

    # Build for specific architecture
    env HOSTS="x86_64-linux-gnu aarch64-linux-gnu x86_64-apple-darwin arm64-apple-darwin" ./contrib/guix/guix-build

Supported host triplets:

- `x86_64-linux-gnu` - Linux x86_64
- `aarch64-linux-gnu` - Linux ARM64
- `x86_64-apple-darwin` - macOS x86_64
- `arm64-apple-darwin` - macOS ARM64
- `x86_64-w64-mingw32` - Windows x86_64

The built binaries will be placed in `guix-build-$(git rev-parse --short=12 HEAD)/output/`.

#### Updating containers in docker hub

After every release or upgrade to tapyrus dependencies in depends directory, `tapyrus/tapyrusd` and `tapyrus/builder` containers need to be updated in docker hub. This can be done running the CI jobs configured for the same:
 - [Push Tapyrus Builder Image](https://github.com/chaintope/tapyrus-core/actions/workflows/push_tapyrus-builder_image.yml)
 - [Push Docker Image](https://github.com/chaintope/tapyrus-core/actions/workflows/push_docker_image.yml)