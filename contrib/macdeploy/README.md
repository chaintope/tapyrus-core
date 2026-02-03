# MacOS Deployment

The `macdeployqtplus` script should not be run manually. Instead, after building as usual:

```bash
make deploy
```

When complete, it will have produced `Tapyrus-Core.zip`.

## Build Dependencies

For cross-compiling from Linux, you'll need the following packages:

```bash
sudo apt-get install -y cmake python3-dev python3-setuptools clang lld llvm llvm-dev zip
```

## SDK Extraction

### Step 1: Obtaining `Xcode.app`

A free Apple Developer Account is required to proceed.

Our macOS SDK can be extracted from Xcode_26.2. Download the
[Xcode_26.2 Apple silicon](https://download.developer.apple.com/Developer_Tools/Xcode_26.2/Xcode_26.2_Apple_silicon.xip).
version if you're running Xcode 26 on an Apple silicon Mac. Download the [Xcode_26.2 Universal] (https://download.developer.apple.com/Developer_Tools/Xcode_26.2/Xcode_26.2_Universal.xip) version if you're running on an Intel-based Mac computer, or if you plan to share this version with other developers.

Alternatively, after logging in to your account go to 'Downloads', then 'View More'
and search for `Xcode 26.2`. Then choose the download file suitable for your host.

An Apple ID and cookies enabled for the hostname are needed to download this.

The `sha256sum` of the downloaded Xcode_26.2_Apple_silicon XIP archive should be `6df31d543c641fe8d18900f716113c76e7a0ec2d3b39ce8c86f30e2602f6b552` and that of Xcode_26.2_Universal XIP archive should be `8f29ab6a9ac6670d3cf53545ffdb1c317d11607fa8db38fc56d3391df7783fbd`

To extract the `.xip` on Linux:

```bash
# Install/clone tools needed for extracting Xcode.app
apt install cpio
git clone https://github.com/bitcoin-core/apple-sdk-tools.git

# Unpack the .xip and place the resulting Xcode.app in your current
# working directory
python3 apple-sdk-tools/extract_xcode.py -f Xcode_26.2_Apple_silicon.xip| cpio -d -i
```

On macOS:

```bash
xip -x Xcode_26.2_Apple_silicon.xip
```

### Step 2: Generating the SDK tarball from `Xcode.app`

To generate the SDK, run the script [`gen-sdk`](./gen-sdk) with the
path to `Xcode.app` (extracted in the previous stage) as the first argument.

```bash
./contrib/macdeploy/gen-sdk '/path/to/Xcode.app'
```

The generated archive should be: `Xcode-26.2-17C52-extracted-SDK-with-libcxx-headers.tar.gz`.
The `sha256sum` should be `5492ac46c59fc3aa4b227b7e6b457904be72525df3caa5ae1b3e0a97da140f46`.
