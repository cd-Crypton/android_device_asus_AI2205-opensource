/******************************************************************************
 *
 *  Copyright 2018 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *
 *  Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *  Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following
 *  disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *
 *  Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *  contributors may be used to endorse or promote products derived
 *  from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include "database_builder.h"

#include "bt_trace.h"
#include "stack/include/gattdefs.h"

#include <base/logging.h>
#include <algorithm>

using bluetooth::Uuid;

namespace gatt {

void DatabaseBuilder::AddService(uint16_t handle, uint16_t end_handle,
                                 const Uuid& uuid, bool is_primary) {
  // general case optimization - we add services in order
  if (database.services.empty() ||
      database.services.back().end_handle < handle) {
    database.services.emplace_back(Service{.handle = handle,
                                           .end_handle = end_handle,
                                           .is_primary = is_primary,
                                           .uuid = uuid});
  } else {
    auto& vec = database.services;

    // Find first service whose start handle is bigger than new service handle
    auto it = std::lower_bound(
        vec.begin(), vec.end(), handle,
        [](Service s, uint16_t handle) { return s.end_handle < handle; });

    // Insert new service just before it
    vec.emplace(it, Service{.handle = handle,
                            .end_handle = end_handle,
                            .is_primary = is_primary,
                            .uuid = uuid});
  }

  services_to_discover.insert({handle, end_handle});
}

void DatabaseBuilder::AddIncludedService(uint16_t handle, const Uuid& uuid,
                                         uint16_t start_handle,
                                         uint16_t end_handle) {
  Service* service = FindService(database.services, handle);
  if (!service) {
    LOG(ERROR) << "Illegal action to add to non-existing service!";
    return;
  }

  /* We discover all Primary Services first. If included service was not seen
   * before, it must be a Secondary Service */
  if (!FindService(database.services, start_handle)) {
    AddService(start_handle, end_handle, uuid, false /* not primary */);
  }

  service->included_services.push_back(IncludedService{
      .handle = handle,
      .uuid = uuid,
      .start_handle = start_handle,
      .end_handle = end_handle,
  });
}

void DatabaseBuilder::AddCharacteristic(uint16_t handle, uint16_t value_handle,
                                        const Uuid& uuid, uint8_t properties) {
  Service* service = FindService(database.services, handle);
  if (!service) {
    LOG(ERROR) << "Illegal action to add to non-existing service!";
    return;
  }

  if (service->end_handle < value_handle)
    LOG(WARNING) << "Remote device violates spec: value_handle="
                 << loghex(value_handle) << " is after service end_handle="
                 << loghex(service->end_handle);

  service->characteristics.emplace_back(
      Characteristic{.declaration_handle = handle,
                     .value_handle = value_handle,
                     .properties = properties,
                     .uuid = uuid});
  return;
}

void DatabaseBuilder::AddDescriptor(uint16_t handle, const Uuid& uuid) {
  Service* service = FindService(database.services, handle);
  if (!service) {
    LOG(ERROR) << "Illegal action to add to non-existing service!";
    return;
  }

  if (service->characteristics.empty()) {
    LOG(ERROR) << __func__
               << ": Illegal action to add to non-existing characteristic!";
    return;
  }

  Characteristic* char_node = &service->characteristics.front();
  for (auto it = service->characteristics.begin();
       it != service->characteristics.end(); it++) {
    if (it->declaration_handle > handle) break;
    char_node = &(*it);
  }

  char_node->descriptors.emplace_back(
      gatt::Descriptor{.handle = handle, .uuid = uuid});

  // We must read value for Characteristic Extended Properties
  if (uuid == Uuid::From16Bit(GATT_UUID_CHAR_EXT_PROP)) {
    descriptor_handles_to_read.emplace_back(handle);
  }
}

bool DatabaseBuilder::StartNextServiceExploration() {
  while (!services_to_discover.empty()) {
    auto handle_range = services_to_discover.begin();
    pending_service = *handle_range;
    services_to_discover.erase(handle_range);

    // Empty service declaration, nothing to explore, skip to next.
    if (pending_service.first == pending_service.second) continue;

    pending_characteristic = HANDLE_MIN;
    return true;
  }
  return false;
}

const std::pair<uint16_t, uint16_t>&
DatabaseBuilder::CurrentlyExploredService() {
  return pending_service;
}

std::pair<uint16_t, uint16_t> DatabaseBuilder::NextDescriptorRangeToExplore() {
  Service* service = FindService(database.services, pending_service.first);
  if (!service || service->characteristics.empty()) {
    return {HANDLE_MAX, HANDLE_MAX};
  }

  for (auto it = service->characteristics.cbegin();
       it != service->characteristics.cend(); it++) {
    if (it->declaration_handle > pending_characteristic) {
      auto next = std::next(it);

      /* Characteristic Declaration is followed by Characteristic Value
       * Declaration, first descriptor is after that, see BT Spect 5.0 Vol 3,
       * Part G 3.3.2 and 3.3.3 */
      uint16_t start = it->declaration_handle + 2;
      uint16_t end;
      if (next != service->characteristics.end())
        end = next->declaration_handle - 1;
      else
        end = service->end_handle;

      // No place for descriptor - skip to next characteristic
      if (start > end) continue;

      pending_characteristic = start;
      return {start, end};
    }
  }

  pending_characteristic = HANDLE_MAX;
  return {HANDLE_MAX, HANDLE_MAX};
}

Descriptor* FindDescriptorByHandle(std::vector<Service>& services,
                                   uint16_t handle) {
  Service* service = FindService(services, handle);
  if (!service) return nullptr;

  Characteristic* char_node = &service->characteristics.front();
  for (auto it = service->characteristics.begin();
       it != service->characteristics.end(); it++) {
    if (it->declaration_handle > handle) break;
    char_node = &(*it);
  }

  for (auto& descriptor : char_node->descriptors) {
    if (descriptor.handle == handle) return &descriptor;
  }

  return nullptr;
}

bool DatabaseBuilder::SetValueOfDescriptors(
    const std::vector<uint16_t>& values) {
  if (values.size() > descriptor_handles_to_read.size()) {
    LOG(ERROR) << "value size: "<<values.size()
               << " <= Descriptor size: "<<descriptor_handles_to_read.size() <<" expected";
    descriptor_handles_to_read.clear();
    return false;
  }

  for (size_t i = 0; i < values.size(); i++) {
    Descriptor* d = FindDescriptorByHandle(database.services,
                                           descriptor_handles_to_read[i]);
    if (!d) {
      LOG(ERROR) << __func__ << "non-existing descriptor!";
      descriptor_handles_to_read.clear();
      return false;
    }

    d->characteristic_extended_properties = values[i];
  }

  descriptor_handles_to_read.erase(
      descriptor_handles_to_read.begin(),
      descriptor_handles_to_read.begin() + values.size());
  return true;
}

bool DatabaseBuilder::RemoveCEPDescriptorsHandlesToRead(uint16_t handle) {
  for ( std::vector<uint16_t>::iterator it = descriptor_handles_to_read.begin();
      it != descriptor_handles_to_read.end(); ++it) {
    if ( handle == *it) {
      descriptor_handles_to_read.erase(it);
      return true;
    }
  }
  return false;
}

bool DatabaseBuilder::InProgress() const { return !database.services.empty(); }

Database DatabaseBuilder::Build() {
  Database tmp = database;
  database.Clear();
  return tmp;
}

void DatabaseBuilder::Clear() { database.Clear(); }

std::string DatabaseBuilder::ToString() const { return database.ToString(); }

}  // namespace gatt
