// Copyright 2021 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

package dev.pigweed.pw_rpc;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;

import com.google.common.collect.ImmutableList;
import com.google.protobuf.MessageLite;
import dev.pigweed.pw_rpc.internal.Packet.PacketType;
import dev.pigweed.pw_rpc.internal.Packet.RpcPacket;
import dev.pigweed.pw_rpc.internal.Packet.RpcPacket.Builder;
import java.util.ArrayList;
import java.util.List;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

public final class EndpointTest {
  @Rule public final MockitoRule mockito = MockitoJUnit.rule();

  private static final Service SERVICE = new Service("pw.rpc.test1.TheTestService",
      Service.unaryMethod("SomeUnary", SomeMessage.parser(), SomeMessage.parser()),
      Service.serverStreamingMethod(
          "SomeServerStreaming", SomeMessage.parser(), SomeMessage.parser()),
      Service.clientStreamingMethod(
          "SomeClientStreaming", SomeMessage.parser(), SomeMessage.parser()),
      Service.bidirectionalStreamingMethod(
          "SomeBidiStreaming", SomeMessage.parser(), SomeMessage.parser()));

  private static final Method METHOD = SERVICE.method("SomeUnary");

  private static final SomeMessage REQUEST_PAYLOAD =
      SomeMessage.newBuilder().setMagicNumber(1337).build();
  private static final byte[] REQUEST = request(REQUEST_PAYLOAD);
  private static final AnotherMessage RESPONSE_PAYLOAD =
      AnotherMessage.newBuilder().setPayload("hello").build();
  private static final int CHANNEL_ID = 555;
  private static final int MAX_CALL_ID = 3;

  @Mock private Channel.Output mockOutput;
  @Mock private StreamObserver<MessageLite> callEvents;

  private final Channel channel = new Channel(CHANNEL_ID, bytes -> mockOutput.send(bytes));
  private final Endpoint endpoint = new Endpoint(CallIdMode.ENABLED, ImmutableList.of(channel));

  private final List<byte[]> sentPackets = new ArrayList<>();
  private final Channel channelWithRecord =
      new Channel(CHANNEL_ID, bytes -> sentPackets.add(bytes));
  private final Endpoint endpointWithRecord =
      new Endpoint(CallIdMode.ENABLED, ImmutableList.of(channelWithRecord), MAX_CALL_ID);

  private static byte[] request(MessageLite payload) {
    return packetBuilder()
        .setType(PacketType.REQUEST)
        .setPayload(payload.toByteString())
        .build()
        .toByteArray();
  }

  private static byte[] cancel() {
    return packetBuilder()
        .setType(PacketType.CLIENT_ERROR)
        .setStatus(Status.CANCELLED.code())
        .build()
        .toByteArray();
  }

  private static RpcPacket.Builder packetBuilder() {
    return RpcPacket.newBuilder()
        .setChannelId(CHANNEL_ID)
        .setCallId(Endpoint.FIRST_CALL_ID)
        .setServiceId(SERVICE.id())
        .setMethodId(METHOD.id());
  }

  private AbstractCall<MessageLite, MessageLite> createCall(Endpoint endpoint, PendingRpc rpc) {
    return StreamObserverCall.getFactory(callEvents).apply(endpoint, rpc);
  }

  @Test
  public void start_succeeds_rpcIsPending() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);

    verify(mockOutput).send(REQUEST);
    assertThat(endpoint.abandon(call)).isTrue();
  }

  @Test
  public void start_succeeds_callIdIsIncreasing() throws Exception {
    endpointWithRecord.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    endpointWithRecord.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    RpcPacket packet1 = RpcPacket.parseFrom(sentPackets.get(0));
    RpcPacket packet2 = RpcPacket.parseFrom(sentPackets.get(1));

    assertThat(packet1.getCallId()).isLessThan(packet2.getCallId());
  }

  @Test
  public void start_succeeds_callIdWraps() throws Exception {
    for (int i = 0; i < MAX_CALL_ID; i++) {
      endpointWithRecord.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    }

    RpcPacket firstPacket = RpcPacket.parseFrom(sentPackets.get(0));
    RpcPacket lastPacket = RpcPacket.parseFrom(sentPackets.get(MAX_CALL_ID - 1));

    assertThat(firstPacket.getCallId()).isEqualTo(lastPacket.getCallId());
  }

  @Test
  public void start_sendingFails_callsHandleError() throws Exception {
    doThrow(new ChannelOutputException()).when(mockOutput).send(any());

    assertThrows(ChannelOutputException.class,
        () -> endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD));

    verify(mockOutput).send(REQUEST);
  }

  @Test
  public void abandon_rpcNoLongerPending() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    assertThat(endpoint.abandon(call)).isTrue();

    assertThat(endpoint.abandon(call)).isFalse();
  }

  @Test
  public void abandon_sendsNoPackets() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    verify(mockOutput).send(REQUEST);
    verifyNoMoreInteractions(mockOutput);

    assertThat(endpoint.abandon(call)).isTrue();
  }

  @Test
  public void cancel_rpcNoLongerPending() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    assertThat(endpoint.cancel(call)).isTrue();

    assertThat(endpoint.abandon(call)).isFalse();
  }

  @Test
  public void cancel_sendsCancelPacket() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.invokeRpc(CHANNEL_ID, METHOD, this::createCall, REQUEST_PAYLOAD);
    assertThat(endpoint.cancel(call)).isTrue();

    verify(mockOutput).send(cancel());
  }

  @Test
  public void open_sendsNoPacketsButRpcIsPending() {
    AbstractCall<MessageLite, MessageLite> call =
        endpoint.openRpc(CHANNEL_ID, METHOD, this::createCall);

    assertThat(call.active()).isTrue();
    assertThat(endpoint.abandon(call)).isTrue();
    verifyNoInteractions(mockOutput);
  }

  @Test
  public void ignoresActionsIfCallIsNotPending() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        createCall(endpoint, PendingRpc.create(channel, METHOD, Endpoint.FIRST_CALL_ID));

    assertThat(endpoint.cancel(call)).isFalse();
    assertThat(endpoint.abandon(call)).isFalse();
    assertThat(endpoint.clientStream(call, REQUEST_PAYLOAD)).isFalse();
    assertThat(endpoint.clientStreamEnd(call)).isFalse();
  }

  @Test
  public void ignoresPacketsIfCallIsNotPending() throws Exception {
    AbstractCall<MessageLite, MessageLite> call =
        createCall(endpoint, PendingRpc.create(channel, METHOD, Endpoint.FIRST_CALL_ID));

    assertThat(endpoint.cancel(call)).isFalse();
    assertThat(endpoint.abandon(call)).isFalse();

    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder()
                       .setType(PacketType.SERVER_STREAM)
                       .setPayload(RESPONSE_PAYLOAD.toByteString())
                       .build()))
        .isTrue();
    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder()
                       .setType(PacketType.RESPONSE)
                       .setPayload(RESPONSE_PAYLOAD.toByteString())
                       .build()))
        .isTrue();
    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder()
                       .setType(PacketType.SERVER_ERROR)
                       .setStatus(Status.ABORTED.code())
                       .build()))
        .isTrue();

    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder()
                       .setType(PacketType.CLIENT_STREAM)
                       .setPayload(REQUEST_PAYLOAD.toByteString())
                       .build()))
        .isTrue();
    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder().setType(PacketType.CLIENT_REQUEST_COMPLETION).build()))
        .isTrue();
    assertThat(endpoint.processClientPacket(call.rpc().method(),
                   packetBuilder()
                       .setType(PacketType.CLIENT_ERROR)
                       .setStatus(Status.ABORTED.code())
                       .build()))
        .isTrue();

    verifyNoInteractions(callEvents);
  }
}
