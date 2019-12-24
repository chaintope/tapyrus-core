Release Process
====================

See also [Tapyrus Signer Release Process](https://github.com/chaintope/tapyrus-signer/blob/master/doc/release-process.md). In general, Tapyrus Signer and Tapyrus Core will be released at the same 
time.    

Before every release candidate:

* Update manpages, see [gen-manpages.sh](https://github.com/chaintope/tapyrus-core/blob/master/contrib/devtools/README.md#gen-manpagessh).

Before every minor and major release:

* Update version in `configure.ac` (don't forget to set `CLIENT_VERSION_IS_RELEASE` to `true`). See also [vertioning rule](https://github.com/chaintope/tapyrus-core/blob/master/doc/tapyrus/versioning_rule.md).
* Write release notes (see below)
* Update version of `contrib/gitian-descriptors/*.yml`: usually one'd want to do this on master after branching off the release - but be sure to at least do it before a new major release
* Update Testnet environment. Tapyrus Testnet environment documents are [here](https://drive.google.com/drive/folders/1-J6GE77dH2u5giKoNsuQr24v02bQJjZp) (This is internal document in Chaintope Inc.)
     * We should bland new testnet for new version if the release doesn't have compatibility and keep old testnet for a while.

### First time / New builders

If you're using the automated script (found in [contrib/gitian-build.py](/contrib/gitian-build.py)), then at this point you should run it with the "--setup" command. Otherwise ignore this.

Check out the source code in the following directory hierarchy.

    cd /path/to/your/toplevel/build
    git clone https://github.com/bitcoin-core/gitian.sigs.git
    git clone https://github.com/bitcoin-core/bitcoin-detached-sigs.git
    git clone https://github.com/devrandom/gitian-builder.git
    git clone https://github.com/bitcoin/bitcoin.git

### Tapyrus maintainers/release engineers, suggestion for writing release notes

Write release notes. git shortlog helps a lot, for example:

    git shortlog --no-merges v(current version, e.g. 0.2.0)..v(new version, e.g. 0.3.0)

Generate list of authors:

    git log --format='- %aN' v(current version, e.g. 0.2.0)..v(new version, e.g. 0.3.1) | sort -fiu

Create release note in `doc/release-notes/tapyrus/`.

Put the summary of the release note on github release like [this](https://github.com/chaintope/tapyrus-core/releases/tag/v0.3.0)
Release tag is going to create when the github release note published. 

### Setup and perform Gitian builds

If you're using the automated script (found in [contrib/gitian-build.py](/contrib/gitian-build.py)), then at this point you should run it with the "--build" command. Otherwise ignore this.

Setup Gitian descriptors:

    pushd ./bitcoin
    export SIGNER="(your Gitian key, ie bluematt, sipa, etc)"
    export VERSION=(new version, e.g. 0.8.0)
    git fetch
    git checkout v${VERSION}
    popd

Ensure your gitian.sigs are up-to-date if you wish to gverify your builds against other Gitian signatures.

    pushd ./gitian.sigs
    git pull
    popd

Ensure gitian-builder is up-to-date:

    pushd ./gitian-builder
    git pull
    popd

### Fetch and create inputs: (first time, or when dependency versions change)

    pushd ./gitian-builder
    mkdir -p inputs
    wget -P inputs https://bitcoincore.org/cfields/osslsigncode-Backports-to-1.7.1.patch
    wget -P inputs http://downloads.sourceforge.net/project/osslsigncode/osslsigncode/osslsigncode-1.7.1.tar.gz
    popd

Create the macOS SDK tarball, see the [macOS readme](README_osx.md) for details, and copy it into the inputs directory.

### Optional: Seed the Gitian sources cache and offline git repositories

NOTE: Gitian is sometimes unable to download files. If you have errors, try the step below.

By default, Gitian will fetch source files as needed. To cache them ahead of time, make sure you have checked out the tag you want to build in bitcoin, then:

    pushd ./gitian-builder
    make -C ../bitcoin/depends download SOURCES_PATH=`pwd`/cache/common
    popd

Only missing files will be fetched, so this is safe to re-run for each build.

NOTE: Offline builds must use the --url flag to ensure Gitian fetches only from local URLs. For example:

    pushd ./gitian-builder
    ./bin/gbuild --url bitcoin=/path/to/bitcoin,signature=/path/to/sigs {rest of arguments}
    popd

The gbuild invocations below <b>DO NOT DO THIS</b> by default.

### Build and sign Bitcoin Core for Linux, Windows, and macOS:

    pushd ./gitian-builder
    ./bin/gbuild --num-make 2 --memory 3000 --commit bitcoin=v${VERSION} ../bitcoin/contrib/gitian-descriptors/gitian-linux.yml
    ./bin/gsign --signer "$SIGNER" --release ${VERSION}-linux --destination ../gitian.sigs/ ../bitcoin/contrib/gitian-descriptors/gitian-linux.yml
    mv build/out/bitcoin-*.tar.gz build/out/src/bitcoin-*.tar.gz ../

    ./bin/gbuild --num-make 2 --memory 3000 --commit bitcoin=v${VERSION} ../bitcoin/contrib/gitian-descriptors/gitian-win.yml
    ./bin/gsign --signer "$SIGNER" --release ${VERSION}-win-unsigned --destination ../gitian.sigs/ ../bitcoin/contrib/gitian-descriptors/gitian-win.yml
    mv build/out/bitcoin-*-win-unsigned.tar.gz inputs/bitcoin-win-unsigned.tar.gz
    mv build/out/bitcoin-*.zip build/out/bitcoin-*.exe ../

    ./bin/gbuild --num-make 2 --memory 3000 --commit bitcoin=v${VERSION} ../bitcoin/contrib/gitian-descriptors/gitian-osx.yml
    ./bin/gsign --signer "$SIGNER" --release ${VERSION}-osx-unsigned --destination ../gitian.sigs/ ../bitcoin/contrib/gitian-descriptors/gitian-osx.yml
    mv build/out/bitcoin-*-osx-unsigned.tar.gz inputs/bitcoin-osx-unsigned.tar.gz
    mv build/out/bitcoin-*.tar.gz build/out/bitcoin-*.dmg ../
    popd

Build output expected:

  1. source tarball (`bitcoin-${VERSION}.tar.gz`)
  2. linux 32-bit and 64-bit dist tarballs (`bitcoin-${VERSION}-linux[32|64].tar.gz`)
  3. windows 32-bit and 64-bit unsigned installers and dist zips (`bitcoin-${VERSION}-win[32|64]-setup-unsigned.exe`, `bitcoin-${VERSION}-win[32|64].zip`)
  4. macOS unsigned installer and dist tarball (`bitcoin-${VERSION}-osx-unsigned.dmg`, `bitcoin-${VERSION}-osx64.tar.gz`)
  5. Gitian signatures (in `gitian.sigs/${VERSION}-<linux|{win,osx}-unsigned>/(your Gitian key)/`)

### Verify other gitian builders signatures to your own. (Optional)

Add other gitian builders keys to your gpg keyring, and/or refresh keys: See `../bitcoin/contrib/gitian-keys/README.md`.

Verify the signatures

    pushd ./gitian-builder
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-linux ../bitcoin/contrib/gitian-descriptors/gitian-linux.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-win-unsigned ../bitcoin/contrib/gitian-descriptors/gitian-win.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-osx-unsigned ../bitcoin/contrib/gitian-descriptors/gitian-osx.yml
    popd

### Next steps:

Commit your signature to gitian.sigs:

    pushd gitian.sigs
    git add ${VERSION}-linux/"${SIGNER}"
    git add ${VERSION}-win-unsigned/"${SIGNER}"
    git add ${VERSION}-osx-unsigned/"${SIGNER}"
    git commit -m "Add ${VERSION} unsigned sigs for ${SIGNER}"
    git push  # Assuming you can push to the gitian.sigs tree
    popd

Codesigner only: Create Windows/macOS detached signatures:
- Only one person handles codesigning. Everyone else should skip to the next step.
- Only once the Windows/macOS builds each have 3 matching signatures may they be signed with their respective release keys.

Codesigner only: Sign the macOS binary:

    transfer bitcoin-osx-unsigned.tar.gz to macOS for signing
    tar xf bitcoin-osx-unsigned.tar.gz
    ./detached-sig-create.sh -s "Key ID"
    Enter the keychain password and authorize the signature
    Move signature-osx.tar.gz back to the gitian host

Codesigner only: Sign the windows binaries:

    tar xf bitcoin-win-unsigned.tar.gz
    ./detached-sig-create.sh -key /path/to/codesign.key
    Enter the passphrase for the key when prompted
    signature-win.tar.gz will be created

Codesigner only: Commit the detached codesign payloads:

    cd ~/bitcoin-detached-sigs
    checkout the appropriate branch for this release series
    rm -rf *
    tar xf signature-osx.tar.gz
    tar xf signature-win.tar.gz
    git add -a
    git commit -m "point to ${VERSION}"
    git tag -s v${VERSION} HEAD
    git push the current branch and new tag

Non-codesigners: wait for Windows/macOS detached signatures:

- Once the Windows/macOS builds each have 3 matching signatures, they will be signed with their respective release keys.
- Detached signatures will then be committed to the [bitcoin-detached-sigs](https://github.com/bitcoin-core/bitcoin-detached-sigs) repository, which can be combined with the unsigned apps to create signed binaries.

Create (and optionally verify) the signed macOS binary:

    pushd ./gitian-builder
    ./bin/gbuild -i --commit signature=v${VERSION} ../bitcoin/contrib/gitian-descriptors/gitian-osx-signer.yml
    ./bin/gsign --signer "$SIGNER" --release ${VERSION}-osx-signed --destination ../gitian.sigs/ ../bitcoin/contrib/gitian-descriptors/gitian-osx-signer.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-osx-signed ../bitcoin/contrib/gitian-descriptors/gitian-osx-signer.yml
    mv build/out/bitcoin-osx-signed.dmg ../bitcoin-${VERSION}-osx.dmg
    popd

Create (and optionally verify) the signed Windows binaries:

    pushd ./gitian-builder
    ./bin/gbuild -i --commit signature=v${VERSION} ../bitcoin/contrib/gitian-descriptors/gitian-win-signer.yml
    ./bin/gsign --signer "$SIGNER" --release ${VERSION}-win-signed --destination ../gitian.sigs/ ../bitcoin/contrib/gitian-descriptors/gitian-win-signer.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-win-signed ../bitcoin/contrib/gitian-descriptors/gitian-win-signer.yml
    mv build/out/bitcoin-*win64-setup.exe ../bitcoin-${VERSION}-win64-setup.exe
    mv build/out/bitcoin-*win32-setup.exe ../bitcoin-${VERSION}-win32-setup.exe
    popd

Commit your signature for the signed macOS/Windows binaries:

    pushd gitian.sigs
    git add ${VERSION}-osx-signed/"${SIGNER}"
    git add ${VERSION}-win-signed/"${SIGNER}"
    git commit -a
    git push  # Assuming you can push to the gitian.sigs tree
    popd

### After 3 or more people have gitian-built and their results match:

- Create `SHA256SUMS.asc` for the builds, and GPG-sign it:

```bash
sha256sum * > SHA256SUMS
```

The list of files should be:
```
bitcoin-${VERSION}-aarch64-linux-gnu.tar.gz
bitcoin-${VERSION}-arm-linux-gnueabihf.tar.gz
bitcoin-${VERSION}-i686-pc-linux-gnu.tar.gz
bitcoin-${VERSION}-x86_64-linux-gnu.tar.gz
bitcoin-${VERSION}-osx64.tar.gz
bitcoin-${VERSION}-osx.dmg
bitcoin-${VERSION}.tar.gz
bitcoin-${VERSION}-win32-setup.exe
bitcoin-${VERSION}-win32.zip
bitcoin-${VERSION}-win64-setup.exe
bitcoin-${VERSION}-win64.zip
```
The `*-debug*` files generated by the gitian build contain debug symbols
for troubleshooting by developers. It is assumed that anyone that is interested
in debugging can run gitian to generate the files for themselves. To avoid
end-user confusion about which file to pick, as well as save storage
space *do not upload these to the bitcoin.org server, nor put them in the torrent*.

- GPG-sign it, delete the unsigned file:
```
gpg --digest-algo sha256 --clearsign SHA256SUMS # outputs SHA256SUMS.asc
rm SHA256SUMS
```
(the digest algorithm is forced to sha256 to avoid confusion of the `Hash:` header that GPG adds with the SHA256 used for the files)
Note: check that SHA256SUMS itself doesn't end up in SHA256SUMS, which is a spurious/nonsensical entry.

- Upload zips and installers, as well as `SHA256SUMS.asc` from last step, to the bitcoin.org server
  into `/var/www/bin/bitcoin-core-${VERSION}`

- A `.torrent` will appear in the directory after a few minutes. Optionally help seed this torrent. To get the `magnet:` URI use:
```bash
transmission-show -m <torrent file>
```
Insert the magnet URI into the announcement sent to mailing lists. This permits
people without access to `bitcoin.org` to download the binary distribution.
Also put it into the `optional_magnetlink:` slot in the YAML file for
bitcoin.org (see below for bitcoin.org update instructions).

- Update bitcoin.org version

  - First, check to see if the Bitcoin.org maintainers have prepared a
    release: https://github.com/bitcoin-dot-org/bitcoin.org/labels/Core

      - If they have, it will have previously failed their Travis CI
        checks because the final release files weren't uploaded.
        Trigger a Travis CI rebuild---if it passes, merge.

  - If they have not prepared a release, follow the Bitcoin.org release
    instructions: https://github.com/bitcoin-dot-org/bitcoin.org/blob/master/docs/adding-events-release-notes-and-alerts.md#release-notes

  - After the pull request is merged, the website will automatically show the newest version within 15 minutes, as well
    as update the OS download links. Ping @saivann/@harding (saivann/harding on Freenode) in case anything goes wrong

- Announce the release:

  - bitcoin-dev and bitcoin-core-dev mailing list

  - Bitcoin Core announcements list https://bitcoincore.org/en/list/announcements/join/

  - bitcoincore.org blog post

  - Update title of #bitcoin on Freenode IRC

  - Optionally twitter, reddit /r/Bitcoin, ... but this will usually sort out itself

  - Notify BlueMatt so that he can start building [the PPAs](https://launchpad.net/~bitcoin/+archive/ubuntu/bitcoin)

  - Archive release notes for the new version to `doc/release-notes/` (branch `master` and branch of the release)

  - Create a [new GitHub release](https://github.com/bitcoin/bitcoin/releases/new) with a link to the archived release notes.

  - Celebrate
