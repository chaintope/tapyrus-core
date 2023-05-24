### MacDeploy ###

This script should not be run manually, instead, after building as usual:

	make deploy

During the process, the disk image window will pop up briefly where the fancy
settings are applied. This is normal, please do not interfere.

xorrisofs is used to create the DMG. xorrisofs cannot compress DMGs, so we use the uncompressed DMG as it is.

Background images and other features can be added to DMG files by inserting a .DS_Store during creation.

When finished, it will produce `Tapyrus-Core.dmg`.
