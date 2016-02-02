#include <array>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include "grpc_test.grpc.pb.h"
#include "grpc++/grpc++.h"

const int port = 12345;
const int proxy_port = 12346;

std::string StringPrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buf[1024];
  vsnprintf(buf, sizeof buf, format, args);
  return buf;
}

class TestServerImpl : public grpc_test::Server::Service {
 public:
  grpc::Status Run(grpc::ServerContext* context,
                   const grpc_test::Request* request,
                   grpc::ServerWriter<grpc_test::Response>* writer) final {
    std::cerr << "Got request: " << request->DebugString() << "\n";
    if (request->hang_seconds() > 0) {
      std::cerr << "Sleeping for " << request->hang_seconds() << " seconds\n";
      while (!context->IsCancelled()) {
        sleep(1);
      }
      std::cerr << "Wakeup\n";
    }
    for (int i = 0; i < request->num_responses(); ++i) {
      grpc_test::Response resp;
      resp.set_data("response");
      writer->Write(resp);
    }
    return grpc::Status::OK;
  }
};

class TestProxyImpl : public grpc_test::Server::Service {
 public:
  grpc::Status Run(grpc::ServerContext* context,
                   const grpc_test::Request* request,
                   grpc::ServerWriter<grpc_test::Response>* writer) final {
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
        StringPrintf("0.0.0.0:%d", port), grpc::InsecureChannelCredentials());
    std::unique_ptr<grpc_test::Server::Stub> stub =
        grpc_test::Server::NewStub(channel);
    std::unique_ptr<grpc::ClientContext> client_context =
        grpc::ClientContext::FromServerContext(*context);
    std::unique_ptr<grpc::ClientReader<grpc_test::Response>> reader =
        stub->Run(client_context.get(), *request);
    grpc_test::Response response;
    while (reader->Read(&response)) {
      writer->Write(response);
    }
    return reader->Finish();
  }
};

// Create a server that hangs, and check that client-side timeout works.
void TimeoutTest() {
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(StringPrintf("0.0.0.0:%d", proxy_port),
                          grpc::InsecureChannelCredentials());
  std::unique_ptr<grpc_test::Server::Stub> stub =
      grpc_test::Server::NewStub(channel);
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(3));
  grpc_test::Request request;
  request.set_data("testdata");
  request.set_hang_seconds(100);
  request.set_num_responses(1);
  std::unique_ptr<grpc::ClientReader<grpc_test::Response>> reader =
      stub->Run(&context, request);
  grpc_test::Response resp;
  if (reader->Read(&resp)) {
    std::cerr << "Unexpected response";
    abort();
  }
  grpc::Status error = reader->Finish();
  if (error.error_code() != grpc::DEADLINE_EXCEEDED) {
    std::cerr << "Unexpected error: " << error.error_code() << ": "
              << error.error_message() << "\n";
    abort();
  }
}

void RunTimeoutTests() {
  for (int i = 0; i < 1000; ++i) {
    TimeoutTest();
  }
}

int main(int argc, char* argv[]) {
  // setenv("GRPC_TRACE", "all", 1);
  grpc_init();

  std::unique_ptr<grpc::Server> server;
  std::unique_ptr<grpc::Server> proxy;

  grpc::ServerBuilder builder;
  builder.AddListeningPort(StringPrintf("0.0.0.0:%d", port),
                           grpc::InsecureServerCredentials());
  TestServerImpl impl;
  builder.RegisterService(&impl);
  server = builder.BuildAndStart();

  grpc::ServerBuilder proxy_builder;
  proxy_builder.AddListeningPort(StringPrintf("0.0.0.0:%d", proxy_port),
                                 grpc::InsecureServerCredentials());
  TestProxyImpl proxy_impl;
  proxy_builder.RegisterService(&proxy_impl);
  proxy = proxy_builder.BuildAndStart();

  std::array<std::thread, 32> threads;
  for (std::thread& t : threads) {
    t = std::thread(RunTimeoutTests);
  }
  for (std::thread& t : threads) {
    t.join();
  }
  return 0;
}
