Release Process
====================

Before every release candidate:

* Update translations (ping wumpus on IRC) see [translation_process.md](https://github.com/chaintope/tapyrus-core/blob/master/doc/translation_process.md#synchronising-translations).
* Update manpages, see [gen-manpages.sh](https://github.com/chaintope/tapyrus-core/blob/master/contrib/devtools/README.md#gen-manpagessh).

Before every minor and major release:

* Update version in `configure.ac` (don't forget to set `CLIENT_VERSION_IS_RELEASE` to `true`)
* Write release notes (see below)

Before every major release:

* Update version of `contrib/gitian-descriptors/*.yml`: usually one'd want to do this on master after branching off the release - but be sure to at least do it before a new major release
* Branch new branch like `v0.5` from `master` for future minor releases (minor releases like `v0.5.1` will be released from the version branch)
* Update `source-branch` in `snap/snapcraft.yml` to match the version branch (commit it directly to the version branch)
* Tag the version branch

### First time / New builders

If you're using the automated script (found in [contrib/gitian-build.py](/contrib/gitian-build.py)), then at this point you should run it with the "--setup" command. Otherwise ignore this.

Check out the source code in the following directory hierarchy.

    cd /path/to/your/toplevel/build
    git clone https://github.com/chaintope/tapyrus-gitian.sigs.git
    git clone https://github.com/devrandom/gitian-builder.git
    git clone https://github.com/chaintope/tapyrus-core.git

### Bitcoin maintainers/release engineers, suggestion for writing release notes

Write release notes. git shortlog helps a lot, for example:

    git shortlog --no-merges v(current version, e.g. 0.7.2)..v(new version, e.g. 0.8.0)

(or ping @wumpus on IRC, he has specific tooling to generate the list of merged pulls
and sort them into categories based on labels)

Generate list of authors:

    git log --format='- %aN' v(current version, e.g. 0.16.0)..v(new version, e.g. 0.16.1) | sort -fiu

Tag version (or release candidate) in git

    git tag -s v(new version, e.g. 0.8.0)

### Initial Gitian Setup

You can use automated script (found in [contrib/gitian-build.py](/contrib/gitian-build.py)).

    $ cp tapyrus-core/contrib/gitian-build.py .
    $ ./gitian-build.py -d --setup

`-d` is an option to use Docker for build.

In order to sign gitian builds on your host machine, which has your PGP key, fork the gitian.sigs repository and clone it on your host machine:

    $ export NAME=satoshi
    $ git clone git@github.com:$NAME/tapyrus-gitian.sigs.git
    $ git remote add $NAME git@github.com:$NAME/tapyrus-gitian.sigs.git

Where `satoshi` is your GitHub name.

#### macOS code setup

In order to builds for macOS, you need to download the free SDK and extract a file. The steps are described:

    $ mkdir -p gitian-builder/inputs
    $ cp 'path/to/extracted-SDK-file' gitian-builder/inputs

In this version, needs MacOSX10.11.sdk.tar.gz as `extracted-SDK-file`.

### Build binaries

To build the most recent tag:

    $ export NAME=satoshi
    $ export VERSION=0.4.1
    $ ./gitian-build.py --detach-sign --no-commit -d -b $NAME $VERSION

You can specify build os using `-o` option. `lws` means each os, `l = linux` and `w = windows`, `m = macOS`.
