.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. kevacoin_qt test_kevacoin_qt
clean: FORCE
	$(MAKE) -C .. kevacoin_qt_clean test_kevacoin_qt_clean
check: FORCE
	$(MAKE) -C .. test_kevacoin_qt_check
kevacoin-qt kevacoin-qt.exe: FORCE
	 $(MAKE) -C .. kevacoin_qt
apk: FORCE
	$(MAKE) -C .. kevacoin_qt_apk
