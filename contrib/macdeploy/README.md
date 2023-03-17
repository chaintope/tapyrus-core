### MacDeploy ###

For Snow Leopard (which uses [Python 2.6](http://www.python.org/download/releases/2.6/)), you will need the param_parser package:
	
	sudo easy_install argparse

This script should not be run manually, instead, after building as usual:

	make deploy

During the process, the disk image window will pop up briefly where the fancy
settings are applied. This is normal, please do not interfere.

xorrisofs is used to create the DMG.

xorrisofs cannot compress DMGs, so afterwards, the DMG tool from the libdmg-hfsplus project is used to compress it. There are several bugs in this tool and its maintainer has seemingly abandoned the project.

The DMG tool has the ability to create DMGs from scratch as well, but this functionality is broken. Only the compression feature is currently used. Ideally, the creation could be fixed and xorrisofs would no longer be necessary.

Background images and other features can be added to DMG files by inserting a .DS_Store during creation.

When finished, it will produce `Bitcoin-Core.dmg`.

