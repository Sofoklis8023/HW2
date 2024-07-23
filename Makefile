# Ορίζουμε τις μεταβλητές που θα χρησιμοποιήσουμε
CC = gcc
CFLAGS =  -g
# Ορίζουμε τους φακέλους
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin


# Ορίζουμε τα targets και τις εξαρτήσεις τους

all: $(BIN_DIR)/jobCommander $(BIN_DIR)/jobExecutorServer 

$(BIN_DIR)/jobCommander: $(BUILD_DIR)/jobCommander.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/jobCommander $(BUILD_DIR)/jobCommander.o

$(BUILD_DIR)/jobCommander.o: $(SRC_DIR)/jobCommander.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/jobCommander.o $(SRC_DIR)/jobCommander.c

$(BIN_DIR)/jobExecutorServer: $(BUILD_DIR)/jobExecutorServer.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/jobExecutorServer $(BUILD_DIR)/jobExecutorServer.o -pthread

$(BUILD_DIR)/jobExecutorServer.o: $(SRC_DIR)/jobExecutorServer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/jobExecutorServer.o $(SRC_DIR)/jobExecutorServer.c	

# Δημιουργία των φακέλων αν δεν υπάρχουν
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

#Καθαρισμός για τα αρχεία object και τα εκτελέσιμα
clean:
	rm -f $(BUILD_DIR)/*.o $(BIN_DIR)/jobCommander $(BIN_DIR)/jobExecutorServer
