all:
	$(MAKE) -C driver
	$(MAKE) -C app

clean:
	$(MAKE) -C driver clean
	$(MAKE) -C app clean
