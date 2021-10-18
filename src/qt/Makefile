.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. falcon_qt test_falcon_qt
clean: FORCE
	$(MAKE) -C .. falcon_qt_clean test_falcon_qt_clean
check: FORCE
	$(MAKE) -C .. test_falcon_qt_check
bitcoin-qt bitcoin-qt.exe: FORCE
	 $(MAKE) -C .. falcon_qt
