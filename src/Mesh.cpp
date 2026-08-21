#include "Mesh.h"
#include <string.h>

namespace mesh {

void Mesh::begin() {
  Dispatcher::begin();
}

void Mesh::loop() {
  Dispatcher::loop();
}

bool Mesh::allowPacketForward(const mesh::Packet* packet) { 
  return false;  // by default, Transport NOT enabled
}

uint32_t Mesh::getRetransmitDelay(const mesh::Packet* packet) { 
  uint32_t t = (_radio->getEstAirtimeFor(packet->getRawLength()) * 52 / 50) / 2;
  return _rng->nextInt(0, 5) * t;
}

uint32_t Mesh::getDirectRetransmitDelay(const Packet* packet) {
  return 0;  // by default, no delay
}

uint8_t Mesh::getExtraAckTransmitCount() const {
  return 0;
}

uint32_t Mesh::getCADFailRetryDelay() const {
  return _rng->nextInt(1, 4) * 120;
}

int Mesh::searchPeersByHash(const uint8_t* hash) {
  return 0;  // not found
}

int Mesh::searchChannelsByHash(const uint8_t* hash, GroupChannel channels[], int max_matches) {
  return 0;  // not found
}

DispatcherAction Mesh::onRecvPacket(Packet* pkt) {
  if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (pkt->path_len < MAX_PATH_SIZE) {
      uint8_t i = 0;
      uint32_t trace_tag;
      memcpy(&trace_tag, &pkt->payload[i], 4); i += 4;
      uint32_t auth_code;
      memcpy(&auth_code, &pkt->payload[i], 4); i += 4;
      uint8_t flags = pkt->payload[i++];
      uint8_t path_sz = flags & 0x03;

      uint8_t len = pkt->payload_len - i;
      uint16_t offset = (uint16_t)pkt->path_len << path_sz;
      if (offset >= len) {   // TRACE has reached end of given path
        onTraceRecv(pkt, trace_tag, auth_code, flags, pkt->path, &pkt->payload[i], len);
      } else if (self_id.isHashMatch(&pkt->payload[i + offset], 1 << path_sz) && allowPacketForward(pkt) && !_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        pkt->path[pkt->path_len++] = (int8_t) (pkt->getSNR() * 4);

        uint32_t d = getDirectRetransmitDelay(pkt);
        return ACTION_RETRANSMIT_DELAYED(5, d);
      }
    }
    return ACTION_RELEASE;
  }

  if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_CONTROL && (pkt->payload[0] & 0x80) != 0) {
    if (pkt->getPathHashCount() == 0) {
      onControlDataRecv(pkt);
    }
    return ACTION_RELEASE;
  }

  if (pkt->isRouteDirect() && pkt->getPathHashCount() > 0) {
    if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i <= pkt->payload_len) {
        onAckRecv(pkt, ack_crc);
      }
    }

    if (self_id.isHashMatch(pkt->path, pkt->getPathHashSize()) && allowPacketForward(pkt)) {
      if (pkt->getPayloadType() == PAYLOAD_TYPE_MULTIPART) {
        return forwardMultipartDirect(pkt);
      } else if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
        if (!_tables->wasSeen(pkt)) {
          _tables->markSeen(pkt);
          removeSelfFromPath(pkt);
          routeDirectRecvAcks(pkt, 0);
        }
        return ACTION_RELEASE;
      }

      if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        removeSelfFromPath(pkt);

        uint32_t d = getDirectRetransmitDelay(pkt);
        return ACTION_RETRANSMIT_DELAYED(0, d);
      }
    }
    return ACTION_RELEASE;
  }

  if (pkt->isRouteFlood() && filterRecvFloodPacket(pkt)) return ACTION_RELEASE;

  DispatcherAction action = ACTION_RELEASE;

  switch (pkt->getPayloadType()) {
    case PAYLOAD_TYPE_ACK: {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i > pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete ACK packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        onAckRecv(pkt, ack_crc);
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_PATH:
    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG: {
      int i = 0;
      uint8_t dest_hash = pkt->payload[i++];
      uint8_t src_hash = pkt->payload[i++];

      uint8_t* rawData = &pkt->payload[i];
      int len = pkt->payload_len - i;

      if (len <= 0) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);

        if (self_id.isHashMatch(&dest_hash)) {
          int num = searchPeersByHash(&src_hash);
          bool found = false;

          for (int j = 0; j < num; j++) {
            uint8_t data[MAX_PACKET_PAYLOAD];
            memcpy(data, rawData, len);

            if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH) {
              int k = 0;
              uint8_t path_len = data[k++];
              if (!Packet::isValidPathLen(path_len)) {
                MESH_DEBUG_PRINTLN("%s PAYLOAD_TYPE_PATH, bad path_len: %u", getLogDateTime(), (uint32_t)path_len);
                break;
              }
              uint8_t hash_size = (path_len >> 6) + 1;
              uint8_t hash_count = path_len & 63;
              uint8_t* path = &data[k]; k += hash_size * hash_count;
              uint8_t extra_type = data[k++] & 0x0F;
              uint8_t* extra = &data[k];
              uint8_t extra_len = len - k;

              if (onPeerPathRecv(pkt, j, path, path_len, extra_type, extra, extra_len)) {
                if (pkt->isRouteFlood()) {
                  mesh::Packet* rpath = createPathReturn(&src_hash, pkt->path, pkt->path_len, 0, NULL, 0);
                  if (rpath) sendDirect(rpath, path, path_len, 500);
                }
              }
            } else {
              onPeerDataRecv(pkt, pkt->getPayloadType(), j, data, len);
            }
            found = true;
            break;
          }

          if (found) {
            pkt->markDoNotRetransmit();
          } else {
            MESH_DEBUG_PRINTLN("%s recv matches no peers, src_hash=%02X", getLogDateTime(), (uint32_t)src_hash);
          }
        }
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_ANON_REQ: {
      int i = 0;
      uint8_t dest_hash = pkt->payload[i++];
      uint8_t* sender_pub_key = &pkt->payload[i]; i += PUB_KEY_SIZE;

      uint8_t* rawData = &pkt->payload[i];
      int len = pkt->payload_len - i;

      if (len <= 0) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        if (self_id.isHashMatch(&dest_hash)) {
          Identity sender(sender_pub_key);

          uint8_t data[MAX_PACKET_PAYLOAD];
          memcpy(data, rawData, len);

          onAnonDataRecv(pkt, sender, data, len);
          pkt->markDoNotRetransmit();
        }
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_GRP_DATA: 
    case PAYLOAD_TYPE_GRP_TXT: {
      int i = 0;
      uint8_t channel_hash = pkt->payload[i++];

      uint8_t* rawData = &pkt->payload[i];
      int len = pkt->payload_len - i;

      if (len <= 0) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        GroupChannel channels[4];
        int num = searchChannelsByHash(&channel_hash, channels, 4);

        if (num > 0) {
          uint8_t data[MAX_PACKET_PAYLOAD];
          memcpy(data, rawData, len);
          onGroupDataRecv(pkt, pkt->getPayloadType(), channels[0], data, len);
        }
        action = routeRecvPacket(pkt);
      } else {
        // --- REPEAT COUNTER INTERCEPT FOR UNENCRYPTED BUILDS ---
        // Packet was ALREADY SEEN, but if it has path hashes attached (getPathHashCount() > 0),
        // a repeater in range just rebroadcasted our group message!
        if (pkt->getPathHashCount() > 0) {
          GroupChannel channels[1];
          int num = searchChannelsByHash(&channel_hash, channels, 1);
          if (num > 0) {
            uint8_t data[MAX_PACKET_PAYLOAD];
            memcpy(data, rawData, len);
            // Re-trigger group data receiver to pass updated path_len to serial/app interface
            onGroupDataRecv(pkt, pkt->getPayloadType(), channels[0], data, len);
          }
        }
      }
      break;
    }
    case PAYLOAD_TYPE_ADVERT: {
      int i = 0;
      Identity id;
      memcpy(id.pub_key, &pkt->payload[i], PUB_KEY_SIZE); i += PUB_KEY_SIZE;

      uint32_t timestamp;
      memcpy(&timestamp, &pkt->payload[i], 4); i += 4;
      const uint8_t* signature = &pkt->payload[i]; i += SIGNATURE_SIZE;

      if (i > pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete advertisement packet", getLogDateTime());
      } else if (self_id.matches(id.pub_key)) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): receiving SELF advert packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        uint8_t* app_data = &pkt->payload[i];
        int app_data_len = pkt->payload_len - i;
        if (app_data_len > MAX_ADVERT_DATA_SIZE) { app_data_len = MAX_ADVERT_DATA_SIZE; }

        bool is_ok;
        {
          uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
          int msg_len = 0;
          memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
          memcpy(&message[msg_len], &timestamp, 4); msg_len += 4;
          memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

          is_ok = id.verify(signature, message, msg_len);
        }
        if (is_ok) {
          MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): valid advertisement received!", getLogDateTime());
          onAdvertRecv(pkt, id, timestamp, app_data, app_data_len);
          action = routeRecvPacket(pkt);
        } else {
          MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): received advertisement with forged signature! (app_data_len=%d)", getLogDateTime(), app_data_len);
        }
      }
      break;
    }
    case PAYLOAD_TYPE_RAW_CUSTOM: {
      if (pkt->isRouteDirect() && !_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        onRawDataRecv(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_MULTIPART:
      if (pkt->payload_len > 2) {
        uint8_t type = pkt->payload[0] & 0x0F;

        if (type == PAYLOAD_TYPE_ACK && pkt->payload_len >= 5) {
          Packet tmp;
          tmp.header = pkt->header;
          tmp.path_len = Packet::copyPath(tmp.path, pkt->path, pkt->path_len);
          tmp.payload_len = pkt->payload_len - 1;
          memcpy(tmp.payload, &pkt->payload[1], tmp.payload_len);

          if (!_tables->wasSeen(&tmp)) {
            _tables->markSeen(&tmp);
            uint32_t ack_crc;
            memcpy(&ack_crc, tmp.payload, 4);

            onAckRecv(&tmp, ack_crc);
          }
        }
      }
      break;

    default:
      MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): unknown payload type, header: %d", getLogDateTime(), (int) pkt->header);
      break;
  }
  return action;
}

void Mesh::removeSelfFromPath(Packet* pkt) {
  pkt->setPathHashCount(pkt->getPathHashCount() - 1);

  uint8_t sz = pkt->getPathHashSize();
  for (int k = 0; k < pkt->getPathHashCount() * sz; k += sz) {
    memcpy(&pkt->path[k], &pkt->path[k + sz], sz);
  }
}

DispatcherAction Mesh::routeRecvPacket(Packet* packet) {
  uint8_t n = packet->getPathHashCount();
  if (packet->isRouteFlood() && !packet->isMarkedDoNotRetransmit()
    && (n + 1) * packet->getPathHashSize() <= MAX_PATH_SIZE && allowPacketForward(packet)) {
    self_id.copyHashTo(&packet->path[n * packet->getPathHashSize()], packet->getPathHashSize());
    packet->setPathHashCount(n + 1);

    uint32_t d = getRetransmitDelay(packet);
    return ACTION_RETRANSMIT_DELAYED(packet->getPathHashCount(), d);
  }
  return ACTION_RELEASE;
}

DispatcherAction Mesh::forwardMultipartDirect(Packet* pkt) {
  uint8_t remaining = pkt->payload[0] >> 4;
  uint8_t type = pkt->payload[0] & 0x0F;

  if (type == PAYLOAD_TYPE_ACK && pkt->payload_len >= 5) {
    Packet tmp;
    tmp.header = pkt->header;
    tmp.path_len = Packet::copyPath(tmp.path, pkt->path, pkt->path_len);
    tmp.payload_len = pkt->payload_len - 1;
    memcpy(tmp.payload, &pkt->payload[1], tmp.payload_len);

    if (!_tables->wasSeen(&tmp)) {
      _tables->markSeen(&tmp);
      removeSelfFromPath(&tmp);
      routeDirectRecvAcks(&tmp, ((uint32_t)remaining + 1) * 300);
    }
  }
  return ACTION_RELEASE;
}

void Mesh::routeDirectRecvAcks(Packet* packet, uint32_t delay_millis) {
  if (!packet->isMarkedDoNotRetransmit()) {
    uint8_t extra = getExtraAckTransmitCount();
    while (extra > 0) {
      delay_millis += getDirectRetransmitDelay(packet) + 300;
      auto a1 = createMultiAck(packet->payload, packet->payload_len, extra);
      if (a1) {
        a1->path_len = Packet::copyPath(a1->path, packet->path, packet->path_len);
        a1->header &= ~PH_ROUTE_MASK;
        a1->header |= ROUTE_TYPE_DIRECT;
        sendPacket(a1, 0, delay_millis);
      }
      extra--;
    }

    auto a2 = createAck(packet->payload, packet->payload_len);
    if (a2) {
      a2->path_len = Packet::copyPath(a2->path, packet->path, packet->path_len);
      a2->header &= ~PH_ROUTE_MASK;
      a2->header |= ROUTE_TYPE_DIRECT;
      sendPacket(a2, 0, delay_millis);
    }
  }
}

Packet* Mesh::createAdvert(const LocalIdentity& id, const uint8_t* app_data, size_t app_data_len) {
  if (app_data_len > MAX_ADVERT_DATA_SIZE) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAdvert(): error, packet pool empty", getLogDateTime());
    return NULL;
  }

  packet->header = (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);

  int len = 0;
  memcpy(&packet->payload[len], id.pub_key, PUB_KEY_SIZE); len += PUB_KEY_SIZE;

  uint32_t emitted_timestamp = _rtc->getCurrentTime();
  memcpy(&packet->payload[len], &emitted_timestamp, 4); len += 4;

  uint8_t* signature = &packet->payload[len]; len += SIGNATURE_SIZE;

  memcpy(&packet->payload[len], app_data, app_data_len); len += app_data_len;

  packet->payload_len = len;

  {
    uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
    int msg_len = 0;
    memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
    memcpy(&message[msg_len], &emitted_timestamp, 4); msg_len += 4;
    memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

    id.sign(signature, message, msg_len);
  }

  return packet;
}

Packet* Mesh::createPathReturn(const Identity& dest, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t* extra, size_t extra_len) {
  uint8_t dest_hash[PATH_HASH_SIZE];
  dest.copyHashTo(dest_hash);
  return createPathReturn(dest_hash, path, path_len, extra_type, extra, extra_len);
}

Packet* Mesh::createPathReturn(const uint8_t* dest_hash, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t* extra, size_t extra_len) {
  uint8_t path_hash_size = (path_len >> 6) + 1;
  uint8_t path_hash_count = path_len & 63;

  if (PATH_HASH_SIZE + PATH_HASH_SIZE + path_hash_count * path_hash_size + extra_len + 5 > MAX_PACKET_PAYLOAD) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createPathReturn(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT);

  int len = 0;
  memcpy(&packet->payload[len], dest_hash, PATH_HASH_SIZE); len += PATH_HASH_SIZE;
  len += self_id.copyHashTo(&packet->payload[len]);

  packet->payload[len++] = path_len;
  memcpy(&packet->payload[len], path, path_hash_count * path_hash_size); len += path_hash_count * path_hash_size;

  if (extra_len > 0) {
    packet->payload[len++] = extra_type;
    memcpy(&packet->payload[len], extra, extra_len); len += extra_len;
  } else {
    packet->payload[len++] = 0xFF;
    getRNG()->random(&packet->payload[len], 4); len += 4;
  }

  packet->payload_len = len;
  return packet;
}

Packet* Mesh::createDatagram(uint8_t type, const Identity& dest, const uint8_t* data, size_t data_len) {
  if (type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE) {
    if (PATH_HASH_SIZE + PATH_HASH_SIZE + data_len > MAX_PACKET_PAYLOAD) return NULL;
  } else {
    return NULL;
  }

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);

  int len = 0;
  len += dest.copyHashTo(&packet->payload[len]);
  len += self_id.copyHashTo(&packet->payload[len]);
  memcpy(&packet->payload[len], data, data_len); len += data_len;

  packet->payload_len = len;
  return packet;
}

Packet* Mesh::createAnonDatagram(uint8_t type, const LocalIdentity& sender, const Identity& dest, const uint8_t* data, size_t data_len) {
  if (type == PAYLOAD_TYPE_ANON_REQ) {
    if (PATH_HASH_SIZE + PUB_KEY_SIZE + data_len > MAX_PACKET_PAYLOAD) return NULL;
  } else {
    return NULL;
  }

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAnonDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);

  int len = 0;
  len += dest.copyHashTo(&packet->payload[len]);
  memcpy(&packet->payload[len], sender.pub_key, PUB_KEY_SIZE); len += PUB_KEY_SIZE;
  memcpy(&packet->payload[len], data, data_len); len += data_len;

  packet->payload_len = len;
  return packet;
}

Packet* Mesh::createGroupDatagram(uint8_t type, const GroupChannel& channel, const uint8_t* data, size_t data_len) {
  if (!(type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA)) return NULL;
  if (PATH_HASH_SIZE + data_len > MAX_PACKET_PAYLOAD) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createGroupDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);

  int len = 0;
  memcpy(&packet->payload[len], channel.hash, PATH_HASH_SIZE); len += PATH_HASH_SIZE;
  memcpy(&packet->payload[len], data, data_len); len += data_len;

  packet->payload_len = len;
  return packet;
}

Packet* Mesh::createAck(const uint8_t* ack, uint8_t len) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);

  memcpy(packet->payload, ack, len);
  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createMultiAck(const uint8_t* ack, uint8_t len, uint8_t remaining) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createMultiAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT);

  packet->payload[0] = (remaining << 4) | PAYLOAD_TYPE_ACK;
  memcpy(&packet->payload[1], ack, len);
  packet->payload_len = 1 + len;

  return packet;
}

Packet* Mesh::createRawData(const uint8_t* data, size_t len) {
  if (len > sizeof(Packet::payload)) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createRawData(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT);

  memcpy(packet->payload, data, len);
  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createTrace(uint32_t tag, uint32_t auth_code, uint8_t flags) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createTrace(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT);

  memcpy(packet->payload, &tag, 4);
  memcpy(&packet->payload[4], &auth_code, 4);
  packet->payload[8] = flags;
  packet->payload_len = 9;

  return packet;
}

Packet* Mesh::createControlData(const uint8_t* data, size_t len) {
  if (len > sizeof(Packet::payload)) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createControlData(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT);

  memcpy(packet->payload, data, len);
  packet->payload_len = len;

  return packet;
}

void Mesh::sendFlood(Packet* packet, uint32_t delay_millis, uint8_t path_hash_size) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): TRACE type not supported", getLogDateTime());
    return;
  }
  if (path_hash_size == 0 || path_hash_size > 3) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): invalid path_hash_size", getLogDateTime());
    return;
  }

  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_FLOOD;
  packet->setPathHashSizeAndCount(path_hash_size, 0);

  _tables->markSeen(packet);

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
    pri = 2;
  } else if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
    pri = 3;
  } else {
    pri = 1;
  }
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendFlood(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis, uint8_t path_hash_size) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): TRACE type not supported", getLogDateTime());
    return;
  }
  if (path_hash_size == 0 || path_hash_size > 3) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): invalid path_hash_size", getLogDateTime());
    return;
  }

  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_TRANSPORT_FLOOD;
  packet->transport_codes[0] = transport_codes[0];
  packet->transport_codes[1] = transport_codes[1];
  packet->setPathHashSizeAndCount(path_hash_size, 0);

  _tables->markSeen(packet);

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
    pri = 2;
  } else if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
    pri = 3;
  } else {
    pri = 1;
  }
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendDirect(Packet* packet, const uint8_t* path, uint8_t path_len, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_DIRECT;

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    memcpy(&packet->payload[packet->payload_len], path, path_len);
    packet->payload_len += path_len;

    packet->path_len = 0;
    pri = 5;
  } else {
    packet->path_len = Packet::copyPath(packet->path, path, path_len);
    if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
      pri = 1;
    } else {
      pri = 0;
    }
  }
  _tables->markSeen(packet);
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_DIRECT;

  packet->path_len = 0;

  _tables->markSeen(packet);

  sendPacket(packet, 0, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_TRANSPORT_DIRECT;
  packet->transport_codes[0] = transport_codes[0];
  packet->transport_codes[1] = transport_codes[1];

  packet->path_len = 0;

  _tables->markSeen(packet);

  sendPacket(packet, 0, delay_millis);
}

} // namespace mesh
