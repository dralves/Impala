// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

package com.cloudera.impala.service;

import org.apache.thrift.TDeserializer;
import org.apache.thrift.TException;
import org.apache.thrift.TSerializer;
import org.apache.thrift.protocol.TBinaryProtocol;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.cloudera.impala.common.ImpalaException;
import com.cloudera.impala.common.InternalException;
import com.cloudera.impala.thrift.TQueryRequest;
import com.cloudera.impala.thrift.TCreateQueryExecRequestResult;

/**
 * JNI-callable interface onto a wrapped Frontend instance. The main point is to serialise
 * and deserialise thrift structures between C and Java.
 */
public class JniFrontend {
  private final static Logger LOG = LoggerFactory.getLogger(JniFrontend.class);

  private final static TBinaryProtocol.Factory protocolFactory =
      new TBinaryProtocol.Factory();

  private final Frontend frontend;

  public JniFrontend() {
    frontend = new Frontend();
  }

  public JniFrontend(boolean lazy) {
    frontend = new Frontend(lazy);
  }

  /**
   * Deserialized a serialized form of thriftQueryRequest to TQueryRequest
   */
  private TQueryRequest deserializeTQueryRequest(byte[] thriftQueryRequest)
      throws ImpalaException {
    // TODO: avoid creating deserializer for each query?
    TDeserializer deserializer = new TDeserializer(protocolFactory);

    TQueryRequest request = new TQueryRequest();
    try {
      deserializer.deserialize(request, thriftQueryRequest);
    } catch (TException e) {
      throw new InternalException(e.getMessage());
    }
    LOG.info("creating TQueryExecRequest for " + request.toString());
    return request;
  }

  /**
   * Create a TQueryExecRequest as well as TResultSetMetadata for a given
   * serialized TQueryRequest. The result is returned as a serialized
   * TCreateQueryExecRequestResult.
   * This call is thread-safe.
   */
  public byte[] createQueryExecRequest(byte[] thriftQueryRequest) throws ImpalaException {
    TQueryRequest request = deserializeTQueryRequest(thriftQueryRequest);

    // process front end
    StringBuilder explainString = new StringBuilder();
    TCreateQueryExecRequestResult result =
        frontend.createQueryExecRequest(request, explainString);

    // Print explain string.
    LOG.info(explainString.toString());

    LOG.info("returned TCreateQueryExecRequestResult: " + result.toString());
    // TODO: avoid creating serializer for each query?
    TSerializer serializer = new TSerializer(protocolFactory);
    try {
      return serializer.serialize(result);
    } catch (TException e) {
      throw new InternalException(e.getMessage());
    }
  }

  /**
   * Return an explain plan based on thriftQueryRequest, a serialized TQueryRequest.
   * This call is thread-safe.
   */
  public String getExplainPlan(byte[] thriftQueryRequest) throws ImpalaException {
    TQueryRequest request = deserializeTQueryRequest(thriftQueryRequest);
    String plan = frontend.getExplainString(request);
    LOG.info("Explain plan: " + plan);
    return plan;
  }

  public void resetCatalog() {
    frontend.resetCatalog();
  }
}
