// (c) 2012 Cloudera, Inc. All rights reserved.
//
// This file contains the main() function for the impala daemon process,
// which exports the Thrift services ImpalaService and ImpalaInternalService.

#include <jni.h>
#include <boost/scoped_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include <protocol/TBinaryProtocol.h>
#include <server/TThreadPoolServer.h>
#include <transport/TServerSocket.h>
#include <server/TServer.h>
#include <transport/TTransportUtils.h>
#include <concurrency/PosixThreadFactory.h>

// TODO: fix this: we currently need to include uid-util.h before impala-server.h
#include "util/uid-util.h"
#include "exec/hbase-table-scanner.h"
#include "runtime/hbase-table-cache.h"
#include "codegen/llvm-codegen.h"
#include "common/status.h"
#include "runtime/coordinator.h"
#include "runtime/exec-env.h"
#include "testutil/test-exec-env.h"
#include "util/jni-util.h"
#include "util/logging.h"
#include "util/thrift-util.h"
#include "sparrow/subscription-manager.h"
#include "common/service-ids.h"
#include "service/impala-server.h"
#include "gen-cpp/ImpalaService.h"
#include "gen-cpp/ImpalaInternalService.h"

using namespace impala;
using namespace std;
using namespace sparrow;
using namespace boost;
using namespace apache::thrift::server;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::concurrency;

DEFINE_string(classpath, "", "java classpath");
//DECLARE_string(host);
DEFINE_string(host, "localhost", "The host on which we're running.");
DECLARE_bool(use_statestore);
DECLARE_int32(fe_port);
DECLARE_int32(be_port);

static void RunServer(TServer* server) {
  VLOG_CONNECTION << "started backend server thread";
  server->serve();
}

int main(int argc, char** argv) {
  InitGoogleLoggingSafe(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  InitThriftLogging();
  LlvmCodeGen::InitializeLlvm();
  JniUtil::InitLibhdfs();
  EXIT_IF_ERROR(JniUtil::Init());
  EXIT_IF_ERROR(HBaseTableScanner::Init());
  EXIT_IF_ERROR(HBaseTableCache::Init());

  // start backend service for the coordinator on be_port
  ExecEnv exec_env;
  TServer* fe_server = NULL;
  TServer* be_server = NULL;
  CreateImpalaServer(&exec_env, FLAGS_fe_port, FLAGS_be_port, &fe_server, &be_server);
  thread be_server_thread = thread(&RunServer, be_server);

  EXIT_IF_ERROR(exec_env.StartServices());

  // register be service *after* starting the be server thread and after starting
  // the subscription mgr handler thread
  if (FLAGS_use_statestore) {
    THostPort host_port;
    host_port.port = FLAGS_be_port;
    host_port.host = FLAGS_host;
    // TODO: Unregister on tear-down (after impala service changes)
    Status status =
        exec_env.subscription_mgr()->RegisterService(IMPALA_SERVICE_ID, host_port);

    if (!status.ok()) {
      LOG(ERROR) << "Could not register with state store service: "
                 << status.GetErrorMsg();
    }
  }

  // this blocks until the fe server terminates
  fe_server->serve();

  delete be_server;
  delete fe_server;
}
