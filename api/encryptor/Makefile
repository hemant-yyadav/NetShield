CXX = g++
CXXFLAGS = -std=c++11 -O2
LDFLAGS = -lssl -lcrypto

all: encryptor

encryptor: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o encryptor $(LDFLAGS)

clean:
	rm -f encryptor
