.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. tapyrus_qt test_tapyrus_qt
clean: FORCE
	$(MAKE) -C .. tapyrus_qt_clean test_tapyrus_qt_clean
check: FORCE
	$(MAKE) -C .. test_tapyrus_qt_check
bitcoin-qt bitcoin-qt.exe: FORCE
	 $(MAKE) -C .. tapyrus_qt
