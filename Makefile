GRPC_DIR=$(HOME)/src/__build__/third_party/grpc
PROTOBUF_DIR=$(HOME)/src/__build__/third_party/protobuf-3.0.0-beta-1
PROTOC=$(HOME)/src/__build__/third_party/protobuf-3.0.0-beta-1/bin/protoc

CXXFLAGS=-std=c++11 -g -I$(GRPC_DIR)/include -I$(PROTOBUF_DIR)/include
LDFLAGS=-L$(GRPC_DIR)/lib -L$(PROTOBUF_DIR)/lib -lgrpc++ -lgrpc -lgpr -lssl -lcrypto -lz -lprotobuf -ldl -lpthread

grpc_test: grpc_test.pb.o grpc_test.grpc.pb.o grpc_test.o
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $^

grpc_test.pb.cc grpc_test.grpc.pb.cc: grpc_test.proto
	$(PROTOC) --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_DIR)/bin/grpc_cpp_plugin grpc_test.proto
