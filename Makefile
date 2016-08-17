BIN = simple_sandbox
INSTALL_LOCATION = /usr/local/bin/simple_sandbox

all: $(BIN)

install: $(BIN)
	cp -p $(BIN) $(INSTALL_LOCATION)

$(BIN): main.cc util.h util.cc log.h
	g++ -Wall --std=c++11 main.cc util.cc -o $@
	sudo chown root:root $@
	sudo chmod +s $@

clean:
	rm -f $(BIN);
