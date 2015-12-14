SRC = sandbox.c
BIN = simple_sandbox
INSTALL_LOCATION = /usr/local/bin/simple_sandbox

all: $(BIN)

install: $(BIN)
	cp -p $(BIN) $(INSTALL_LOCATION)

$(BIN): $(SRC)
	gcc -Wall $(SRC) -o $(BIN);
	sudo chown root:root $(BIN);
	sudo chmod +s $(BIN);

clean:
	rm -f $(BIN);
