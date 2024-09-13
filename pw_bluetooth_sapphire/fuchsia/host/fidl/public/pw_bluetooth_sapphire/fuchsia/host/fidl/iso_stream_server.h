// Copyright 2024 The Pigweed Authors
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

#pragma once

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "pw_bluetooth_sapphire/fuchsia/host/fidl/server_base.h"
#include "pw_bluetooth_sapphire/internal/host/common/weak_self.h"
#include "pw_bluetooth_sapphire/internal/host/iso/iso_common.h"

namespace bthost {
class IsoStreamServer
    : public ServerBase<fuchsia::bluetooth::le::IsochronousStream> {
 public:
  explicit IsoStreamServer(
      fidl::InterfaceRequest<fuchsia::bluetooth::le::IsochronousStream> request,
      fit::callback<void()> on_closed_cb);

  void OnStreamEstablished(
      bt::iso::IsoStream::WeakPtr stream_ptr,
      const bt::iso::CisEstablishedParameters& connection_params);

  void OnStreamEstablishmentFailed(pw::bluetooth::emboss::StatusCode status);

  void OnClosed();

  void Close(zx_status_t epitaph);

  using WeakPtr = WeakSelf<IsoStreamServer>::WeakPtr;
  WeakPtr GetWeakPtr() { return weak_self_.GetWeakPtr(); }

 private:
  // fuchsia::bluetooth::le::IsochronousStream overrides:
  void SetupDataPath(
      fuchsia::bluetooth::le::IsochronousStreamSetupDataPathRequest parameters,
      SetupDataPathCallback callback) override;
  void Read(ReadCallback callback) override;
  void handle_unknown_method(uint64_t ordinal, bool has_response) override;

  fit::callback<void()> on_closed_cb_;

  std::optional<bt::iso::IsoStream::WeakPtr> iso_stream_;

  WeakSelf<IsoStreamServer> weak_self_;

  BT_DISALLOW_COPY_ASSIGN_AND_MOVE(IsoStreamServer);
};

}  // namespace bthost