#include <helpers/BaseChatMesh.h>
#include <Utils.h>

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY   300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY     200
#endif

void BaseChatMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}
void BaseChatMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}

mesh::Packet* BaseChatMesh::createSelfAdvert(const char* name) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  {
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

mesh::Packet* BaseChatMesh::createSelfAdvert(const char* name, double lat, double lon) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  {
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name, lat, lon);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

void BaseChatMesh::sendAckTo(const ContactInfo& dest, const uint8_t* ack_hash, uint8_t ack_len) {
  if (dest.out_path_len == OUT_PATH_UNKNOWN) {
    mesh::Packet* ack = createAck(ack_hash, ack_len);
    if (ack) sendFloodScoped(dest, ack, TXT_ACK_DELAY);
  } else {
    uint32_t d = TXT_ACK_DELAY;
    if (getExtraAckTransmitCount() > 0) {
      mesh::Packet* a1 = createMultiAck(ack_hash, ack_len, 1);
      if (a1) sendDirect(a1, dest.out_path, dest.out_path_len, d);
      d += 300;
    }

    mesh::Packet* a2 = createAck(ack_hash, ack_len);
    if (a2) sendDirect(a2, dest.out_path, dest.out_path_len, d);
  }
}

void BaseChatMesh::bootstrapRTCfromContacts() {
  uint32_t latest = 0;
  for (int i = 0; i < num_contacts; i++) {
    if (contacts[i].lastmod > latest) {
      latest = contacts[i].lastmod;
    }
  }
  if (latest != 0) {
    getRTCClock()->setCurrentTime(latest + 1);
  }
}

ContactInfo* BaseChatMesh::allocateContactSlot(bool transient_only) {
  int oldest_idx = -1;
  uint32_t oldest_lastmod = 0xFFFFFFFF;
  if (transient_only) {
    for (int i = 0; i < MAX_ANON_CONTACTS; i++) {
      if (contacts[i].type == ADV_TYPE_NONE && contacts[i].lastmod < oldest_lastmod) {
        oldest_lastmod = contacts[i].lastmod;
        oldest_idx = i;
      }
    }
    if (oldest_idx >= 0) {
      return &contacts[oldest_idx];
    }
  } else {
    if (num_contacts < MAX_ANON_CONTACTS+MAX_CONTACTS) {
      return &contacts[num_contacts++];
    } else if (shouldOverwriteWhenFull()) {
      for (int i = MAX_ANON_CONTACTS; i < num_contacts; i++) {
        bool is_favourite = (contacts[i].flags & 0x01) != 0;
        if (!is_favourite && contacts[i].lastmod < oldest_lastmod) {
          oldest_lastmod = contacts[i].lastmod;
          oldest_idx = i;
        }
      }
      if (oldest_idx >= 0) {
        onContactOverwrite(contacts[oldest_idx].id.pub_key);
        return &contacts[oldest_idx];
      }
    }
  }
  return NULL;
}

void BaseChatMesh::populateContactFromAdvert(ContactInfo& ci, const mesh::Identity& id, const AdvertDataParser& parser, uint32_t timestamp) {
  memset(&ci, 0, sizeof(ci));
  ci.id = id;
  ci.out_path_len = OUT_PATH_UNKNOWN;
  StrHelper::strncpy(ci.name, parser.getName(), sizeof(ci.name));
  ci.type = parser.getType();
  if (parser.hasLatLon()) {
    ci.gps_lat = parser.getIntLat();
    ci.gps_lon = parser.getIntLon();
  }
  ci.last_advert_timestamp = timestamp;
  ci.lastmod = getRTCClock()->getCurrentTime();
}

void BaseChatMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
  AdvertDataParser parser(app_data, app_data_len);
  if (!(parser.isValid() && parser.hasName())) {
    MESH_DEBUG_PRINTLN("onAdvertRecv: invalid app_data, or name is missing: len=%d", app_data_len);
    return;
  }

  ContactInfo* from = NULL;
  for (int i = 0; i < num_contacts; i++) {
    if (id.matches(contacts[i].id)) {
      from = &contacts[i];
      if (timestamp <= from->last_advert_timestamp) {
        MESH_DEBUG_PRINTLN("onAdvertRecv: Possible replay attack, name: %s", from->name);
        return;
      }
      break;
    }
  }

  int plen;
  {
    uint8_t save = packet->header;
    packet->header &= ~PH_ROUTE_MASK;
    packet->header |= ROUTE_TYPE_FLOOD;
    plen = packet->writeTo(temp_buf);
    packet->header = save;
  }

  if (from && from->type == ADV_TYPE_NONE) {
    memset(from, 0, sizeof(*from));
    from = NULL;
  }

  bool is_new = false;
  if (from == NULL) {
    if (!shouldAutoAddContactType(parser.getType())) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);
      return;
    }

    uint8_t max_hops = getAutoAddMaxHops();
    if (max_hops > 0 && packet->getPathHashCount() >= max_hops) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);
      return;
    }

    from = allocateContactSlot();
    if (from == NULL) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);
      onContactsFull();
      MESH_DEBUG_PRINTLN("onAdvertRecv: unable to allocate contact slot for new contact");
      return;
    }
    
    populateContactFromAdvert(*from, id, parser, timestamp);
    from->sync_since = 0;
  }

  putBlobByKey(id.pub_key, PUB_KEY_SIZE, temp_buf, plen);
  StrHelper::strncpy(from->name, parser.getName(), sizeof(from->name));
  from->type = parser.getType();
  if (parser.hasLatLon()) {
    from->gps_lat = parser.getIntLat();
    from->gps_lon = parser.getIntLon();
  }
  from->last_advert_timestamp = timestamp;
  from->lastmod = getRTCClock()->getCurrentTime();

  onDiscoveredContact(*from, is_new, packet->path_len, packet->path);
}

int BaseChatMesh::searchPeersByHash(const uint8_t* hash) {
  int n = 0;
  for (int i = 0; i < num_contacts && n < MAX_SEARCH_RESULTS; i++) {
    if (contacts[i].id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i;
    }
  }
  return n;
}

void BaseChatMesh::onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, uint8_t* data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= num_contacts) {
    MESH_DEBUG_PRINTLN("onPeerDataRecv: Invalid sender idx: %d", i);
    return;
  }

  ContactInfo& from = contacts[i];

  if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);
    uint8_t flags = data[4] >> 2;

    data[len] = 0;

    if (flags == TXT_TYPE_PLAIN) {
      from.lastmod = getRTCClock()->getCurrentTime();
      onMessageRecv(from, packet, timestamp, (const char *) &data[5]);

      int text_len = strlen((char *)&data[5]);
      uint8_t ack_hash[6];
      mesh::Utils::sha256(ack_hash, 4, data, 5 + text_len, from.id.pub_key, PUB_KEY_SIZE);
      ack_hash[4] = data[5 + text_len + 1];
      getRNG()->random(&ack_hash[5], 1);

      if (packet->isRouteFlood()) {
        mesh::Packet* path = createPathReturn(from.id, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 6);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);
      } else {
        sendAckTo(from, ack_hash, 6);
      }
    } else if (flags == TXT_TYPE_CLI_DATA) {
      onCommandDataRecv(from, packet, timestamp, (const char *) &data[5]);

      if (packet->isRouteFlood()) {
        mesh::Packet* path = createPathReturn(from.id, packet->path, packet->path_len, 0, NULL, 0);
        if (path) sendFloodScoped(from, path);
      }
    } else if (flags == TXT_TYPE_SIGNED_PLAIN) {
      if (timestamp > from.sync_since) {
        from.sync_since = timestamp;
      }
      from.lastmod = getRTCClock()->getCurrentTime();
      onSignedMessageRecv(from, packet, timestamp, &data[5], (const char *) &data[9]);

      uint32_t ack_hash;
      mesh::Utils::sha256((uint8_t *) &ack_hash, 4, data, 9 + strlen((char *)&data[9]), self_id.pub_key, PUB_KEY_SIZE);

      if (packet->isRouteFlood()) {
        mesh::Packet* path = createPathReturn(from.id, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 4);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);
      } else {
        sendAckTo(from, (uint8_t *) &ack_hash);
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported message type: %u", (uint32_t) flags);
    }
  } else if (type == PAYLOAD_TYPE_REQ && len > 4) {
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4);
    uint8_t reply_len = onContactRequest(from, sender_timestamp, &data[4], len - 4, temp_buf);
    if (reply_len > 0) {
      if (packet->isRouteFlood()) {
        mesh::Packet* path = createPathReturn(from.id, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, temp_buf, reply_len);
        if (path) sendFloodScoped(from, path, SERVER_RESPONSE_DELAY);
      } else {
        mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, from.id, temp_buf, reply_len);
        if (reply) {
          if (from.out_path_len != OUT_PATH_UNKNOWN) {
            sendDirect(reply, from.out_path, from.out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodScoped(from, reply, SERVER_RESPONSE_DELAY);
          }
        }
      }
    }
  } else if (type == PAYLOAD_TYPE_RESPONSE && len > 0) {
    onContactResponse(from, data, len);
    if (packet->isRouteFlood() && from.out_path_len != OUT_PATH_UNKNOWN) {
      handleReturnPathRetry(from, packet->path, packet->path_len);
    }
  }
}

bool BaseChatMesh::onPeerPathRecv(mesh::Packet* packet, int sender_idx, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= num_contacts) {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: Invalid sender idx: %d", i);
    return false;
  }

  ContactInfo& from = contacts[i];

  return onContactPathRecv(from, packet->path, packet->path_len, path, path_len, extra_type, extra, extra_len);
}

bool BaseChatMesh::onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  from.out_path_len = mesh::Packet::copyPath(from.out_path, out_path, out_path_len);
  from.lastmod = getRTCClock()->getCurrentTime();

  onContactPathUpdated(from);

  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
    if (processAck(extra) != NULL) {
      txt_send_timeout = 0;
    }
  } else if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 0) {
    onContactResponse(from, extra, extra_len);
  }
  return true;
}

void BaseChatMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  ContactInfo* from;
  if ((from = processAck((uint8_t *)&ack_crc)) != NULL) {
    txt_send_timeout = 0;
    packet->markDoNotRetransmit();

    if (packet->isRouteFlood() && from->out_path_len != OUT_PATH_UNKNOWN) {
      handleReturnPathRetry(*from, packet->path, packet->path_len);
    }
  }
}

void BaseChatMesh::handleReturnPathRetry(const ContactInfo& contact, const uint8_t* path, uint8_t path_len) {
  mesh::Packet* rpath = createPathReturn(contact.id, path, path_len, 0, NULL, 0);
  if (rpath) sendDirect(rpath, contact.out_path, contact.out_path_len, 3000);
}

#ifdef MAX_GROUP_CHANNELS
int BaseChatMesh::searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel dest[], int max_matches) {
  int n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS && n < max_matches; i++) {
    if (channels[i].channel.hash[0] == hash[0]) {
      dest[n++] = channels[i].channel;
    }
  }
  return n;
}
#endif

void BaseChatMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) {
  if (type == PAYLOAD_TYPE_GRP_TXT) {
    if (len < 5) {
      MESH_DEBUG_PRINTLN("onGroupDataRecv: dropping short group text payload len=%d", (uint32_t)len);
      return;
    }

    uint8_t txt_type = data[4];
    if ((txt_type >> 2) != 0) {
      MESH_DEBUG_PRINTLN("onGroupDataRecv: dropping unsupported group text type=%d", (uint32_t)txt_type);
      return;
    }

    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;

    onChannelMessageRecv(channel, packet, timestamp, (const char *) &data[5]);
  } else if (type == PAYLOAD_TYPE_GRP_DATA) {
    if (len < 3) {
      MESH_DEBUG_PRINTLN("onGroupDataRecv: dropping short group data payload len=%d", (uint32_t)len);
      return;
    }

    uint16_t data_type = ((uint16_t)data[0]) | (((uint16_t)data[1]) << 8);
    uint8_t data_len = data[2];
    size_t available_len = len - 3;

    if (data_len > available_len) {
      MESH_DEBUG_PRINTLN("onGroupDataRecv: dropping malformed group data type=%d len=%d available=%d",
                         (uint32_t)data_type, (uint32_t)data_len, (uint32_t)available_len);
      return;
    }

    onChannelDataRecv(channel, packet, data_type, &data[3], data_len);
  }
}

mesh::Packet* BaseChatMesh::composeMsgPacket(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char *text, uint32_t& expected_ack) {
  int text_len = strlen(text);
  if (text_len > MAX_TEXT_LEN) return NULL;
  if (attempt > 3 && text_len > MAX_TEXT_LEN-2) return NULL;

  uint8_t temp[5+MAX_TEXT_LEN+1];
  memcpy(temp, &timestamp, 4);
  temp[4] = (attempt & 3);
  memcpy(&temp[5], text, text_len + 1);

  mesh::Utils::sha256((uint8_t *)&expected_ack, 4, temp, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);

  int len = 5 + text_len;
  if (attempt > 3) {
    temp[len++] = 0;
    temp[len++] = attempt;
  }

  return createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, temp, len);
}

int BaseChatMesh::sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& expected_ack, uint32_t& est_timeout) {
  mesh::Packet* pkt = composeMsgPacket(recipient, timestamp, attempt, text, expected_ack);
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());

  int rc;
  if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(recipient, pkt);
    txt_send_timeout = futureMillis(est_timeout = calcFloodTimeoutMillisFor(t));
    rc = MSG_SEND_SENT_FLOOD;
  } else {
    sendDirect(pkt, recipient.out_path, recipient.out_path_len);
    txt_send_timeout = futureMillis(est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len));
    rc = MSG_SEND_SENT_DIRECT;
  }
  return rc;
}

int BaseChatMesh::sendCommandData(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& est_timeout) {
  int text_len = strlen(text);
  if (text_len > MAX_TEXT_LEN) return MSG_SEND_FAILED;

  uint8_t temp[5+MAX_TEXT_LEN+1];
  memcpy(temp, &timestamp, 4);
  temp[4] = (attempt & 3) | (TXT_TYPE_CLI_DATA << 2);
  memcpy(&temp[5], text, text_len + 1);

  auto pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, temp, 5 + text_len);
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
  int rc;
  if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(recipient, pkt);
    txt_send_timeout = futureMillis(est_timeout = calcFloodTimeoutMillisFor(t));
    rc = MSG_SEND_SENT_FLOOD;
  } else {
    sendDirect(pkt, recipient.out_path, recipient.out_path_len);
    txt_send_timeout = futureMillis(est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len));
    rc = MSG_SEND_SENT_DIRECT;
  }
  return rc;
}

bool BaseChatMesh::sendGroupMessage(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name, const char* text, int text_len) {
  uint8_t temp[5+MAX_TEXT_LEN+32];
  memcpy(temp, &timestamp, 4);
  temp[4] = 0;

  sprintf((char *) &temp[5], "%s: ", sender_name);
  char *ep = strchr((char *) &temp[5], 0);
  int prefix_len = ep - (char *) &temp[5];

  if (text_len + prefix_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN - prefix_len;
  memcpy(ep, text, text_len);
  ep[text_len] = 0;

  auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, temp, 5 + prefix_len + text_len);
  if (pkt) {
    sendFloodScoped(channel, pkt);
    return true;
  }
  return false;
}

bool BaseChatMesh::sendGroupData(mesh::GroupChannel& channel, uint8_t* path, uint8_t path_len, uint16_t data_type, const uint8_t* data, int data_len) {
  if (data_len < 0) {
    MESH_DEBUG_PRINTLN("sendGroupData: invalid negative data_len=%d", data_len);
    return false;
  }
  if (data_len > MAX_GROUP_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("sendGroupData: data_len=%d exceeds max=%d", data_len, MAX_GROUP_DATA_LENGTH);
    return false;
  }

  uint8_t temp[3 + MAX_GROUP_DATA_LENGTH];
  temp[0] = (uint8_t)(data_type & 0xFF);
  temp[1] = (uint8_t)(data_type >> 8);
  temp[2] = (uint8_t)data_len;
  if (data_len > 0) memcpy(&temp[3], data, data_len);

  auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_DATA, channel, temp, 3 + data_len);
  if (pkt == NULL) {
    MESH_DEBUG_PRINTLN("sendGroupData: unable to create group datagram, data_len=%d", data_len);
    return false;
  }

  if (path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(channel, pkt);
  } else {
    sendDirect(pkt, path, path_len);
  }

  return true;
}

bool BaseChatMesh::shareContactZeroHop(const ContactInfo& contact) {
  int plen = getBlobByKey(contact.id.pub_key, PUB_KEY_SIZE, temp_buf);
  if (plen == 0) return false;

  auto packet = obtainNewPacket();
  if (packet == NULL) return false;

  packet->readFrom(temp_buf, plen);
  uint16_t codes[2];
  codes[0] = codes[1] = 0;
  sendZeroHop(packet, codes);
  return true;
}

uint8_t BaseChatMesh::exportContact(const ContactInfo& contact, uint8_t dest_buf[]) {
  return getBlobByKey(contact.id.pub_key, PUB_KEY_SIZE, dest_buf);
}

bool BaseChatMesh::importContact(const uint8_t src_buf[], uint8_t len) {
  auto pkt = obtainNewPacket();
  if (pkt) {
    if (pkt->readFrom(src_buf, len) && pkt->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
      pkt->header |= ROUTE_TYPE_FLOOD;
      getTables()->clear(pkt);
      _pendingLoopback = pkt;
      return true;
    } else {
      releasePacket(pkt);
    }
  }
  return false;
}

int BaseChatMesh::sendLogin(const ContactInfo& recipient, const char* password, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    int tlen;
    uint8_t temp[24];
    uint32_t now = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &now, 4);
    if (recipient.type == ADV_TYPE_ROOM) {
      memcpy(&temp[4], &recipient.sync_since, 4);
      int len = strlen(password); if (len > 15) len = 15;
      memcpy(&temp[8], password, len);
      tlen = 8 + len;
    } else {
      int len = strlen(password); if (len > 15) len = 15;
      memcpy(&temp[4], password, len);
      tlen = 4 + len;
    }

    pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, recipient.id, temp, tlen);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int BaseChatMesh::sendAnonReq(const ContactInfo& recipient, const uint8_t* data, uint8_t len, uint32_t& tag, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    uint8_t temp[MAX_PACKET_PAYLOAD];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);
    memcpy(&temp[4], data, len);

    pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, recipient.id, temp, 4 + len);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int BaseChatMesh::sendRequest(const ContactInfo& recipient, const uint8_t* req_data, uint8_t data_len, uint32_t& tag, uint32_t& est_timeout) {
  if (data_len > MAX_PACKET_PAYLOAD - 16) return MSG_SEND_FAILED;

  mesh::Packet* pkt;
  {
    uint8_t temp[MAX_PACKET_PAYLOAD];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);
    memcpy(&temp[4], req_data, data_len);

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, temp, 4 + data_len);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int BaseChatMesh::sendRequest(const ContactInfo& recipient, uint8_t req_type, uint32_t& tag, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    uint8_t temp[13];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);
    temp[4] = req_type;
    memset(&temp[5], 0, 4);
    getRNG()->random(&temp[9], 4);

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, temp, sizeof(temp));
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

bool BaseChatMesh::startConnection(const ContactInfo& contact, uint16_t keep_alive_secs) {
  int use_idx = -1;
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis == 0) {
      use_idx = i;
    } else if (connections[i].server_id.matches(contact.id)) {
      use_idx = i;
      break;
    }
  }
  if (use_idx < 0) {
    return false;
  }
  connections[use_idx].server_id = contact.id;
  uint32_t interval = connections[use_idx].keep_alive_millis = ((uint32_t)keep_alive_secs)*1000;
  connections[use_idx].next_ping = futureMillis(interval);
  connections[use_idx].expected_ack = 0;
  connections[use_idx].last_activity = getRTCClock()->getCurrentTime();
  return true;
}

void BaseChatMesh::stopConnection(const uint8_t* pub_key) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].server_id.matches(pub_key)) {
      connections[i].keep_alive_millis = 0;
      connections[i].next_ping = 0;
      connections[i].expected_ack = 0;
      connections[i].last_activity = 0;
      break;
    }
  }
}

bool BaseChatMesh::hasConnectionTo(const uint8_t* pub_key) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && connections[i].server_id.matches(pub_key)) return true;
  }
  return false;
}

void BaseChatMesh::markConnectionActive(const ContactInfo& contact) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && connections[i].server_id.matches(contact.id)) {
      connections[i].last_activity = getRTCClock()->getCurrentTime();
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);
      break;
    }
  }
}

ContactInfo* BaseChatMesh::checkConnectionsAck(const uint8_t* data) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && memcmp(&connections[i].expected_ack, data, 4) == 0) {
      connections[i].expected_ack = 0;
      connections[i].last_activity = getRTCClock()->getCurrentTime();
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);

      auto id = &connections[i].server_id;
      return lookupContactByPubKey(id->pub_key, PUB_KEY_SIZE);
    }
  }
  return NULL;
}

void BaseChatMesh::checkConnections() {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis == 0) continue;

    uint32_t now = getRTCClock()->getCurrentTime();
    uint32_t expire_secs = (connections[i].keep_alive_millis / 1000) * 5 / 2;
    if (now >= connections[i].last_activity + expire_secs) {
      connections[i].keep_alive_millis = 0;
      connections[i].next_ping = 0;
      connections[i].expected_ack = 0;
      connections[i].last_activity = 0;
      continue;
    }

    if (millisHasNowPassed(connections[i].next_ping)) {
      auto contact = lookupContactByPubKey(connections[i].server_id.pub_key, PUB_KEY_SIZE);
      if (contact == NULL) {
        MESH_DEBUG_PRINTLN("checkConnections(): Keep_alive contact not found!");
        continue;
      }
      if (contact->out_path_len == OUT_PATH_UNKNOWN) {
        MESH_DEBUG_PRINTLN("checkConnections(): Keep_alive contact, no out_path!");
        continue;
      }

      uint8_t data[9];
      uint32_t now_ts = getRTCClock()->getCurrentTimeUnique();
      memcpy(data, &now_ts, 4);
      data[4] = REQ_TYPE_KEEP_ALIVE;
      memcpy(&data[5], &contact->sync_since, 4);
      
      mesh::Utils::sha256((uint8_t *)&connections[i].expected_ack, 4, data, 9, self_id.pub_key, PUB_KEY_SIZE);

      auto pkt = createDatagram(PAYLOAD_TYPE_REQ, contact->id, data, 9);
      if (pkt) {
        sendDirect(pkt, contact->out_path, contact->out_path_len);
      }
      
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);
    }
  }
}

void BaseChatMesh::resetPathTo(ContactInfo& recipient) {
  recipient.out_path_len = OUT_PATH_UNKNOWN;
}

static ContactInfo* table;

static int cmp_adv_timestamp(const void *a, const void *b) {
  int a_idx = *((int *)a);
  int b_idx = *((int *)b);
  if (table[b_idx].last_advert_timestamp > table[a_idx].last_advert_timestamp) return 1;
  if (table[b_idx].last_advert_timestamp < table[a_idx].last_advert_timestamp) return -1;
  return 0;
}

void BaseChatMesh::scanRecentContacts(int last_n, ContactVisitor* visitor) {
  for (int i = 0; i < num_contacts; i++) {
    sort_array[i] = i;
  }
  table = contacts;
  qsort(sort_array, num_contacts, sizeof(sort_array[0]), cmp_adv_timestamp);

  if (last_n == 0) {
    last_n = num_contacts;
  } else {
    if (last_n > num_contacts) last_n = num_contacts;
  }
  for (int i = 0; i < last_n; i++) {
    visitor->onContactVisit(contacts[sort_array[i]]);
  }
}

ContactInfo* BaseChatMesh::searchContactsByPrefix(const char* name_prefix) {
  int len = strlen(name_prefix);
  for (int i = 0; i < num_contacts; i++) {
    auto c = &contacts[i];
    if (memcmp(c->name, name_prefix, len) == 0) return c;
  }
  return NULL;
}

ContactInfo* BaseChatMesh::lookupContactByPubKey(const uint8_t* pub_key, int prefix_len) {
  for (int i = 0; i < num_contacts; i++) {
    auto c = &contacts[i];
    if (memcmp(c->id.pub_key, pub_key, prefix_len) == 0) return c;
  }
  return NULL;
}

bool BaseChatMesh::addContact(const ContactInfo& contact) {
  ContactInfo* dest = allocateContactSlot(contact.type == ADV_TYPE_NONE);
  if (dest) {
    *dest = contact;
    return true;
  }
  return false;
}

bool BaseChatMesh::removeContact(ContactInfo& contact) {
  int idx = 0;
  while (idx < num_contacts && !contacts[idx].id.matches(contact.id)) {
    idx++;
  }
  if (idx >= num_contacts) return false;

  num_contacts--;
  while (idx < num_contacts) {
    contacts[idx] = contacts[idx + 1];
    idx++;
  }
  return true;
}

#ifdef MAX_GROUP_CHANNELS
#include <base64.hpp>

ChannelDetails* BaseChatMesh::addChannel(const char* name, const char* psk_base64) {
  if (num_channels < MAX_GROUP_CHANNELS) {
    auto dest = &channels[num_channels];

    // The channel hash byte MUST be derived the same way every other MeshCore
    // device (and the companion app) derives it -- SHA256 of the PSK -- so that
    // this fork's channel packets are recognized as belonging to that channel.
    // The PSK bytes are used transiently, right here, purely to compute that
    // public, non-secret identifying tag; they are never stored and never used
    // to encrypt message content, which stays plaintext.
    uint8_t psk[32];
    int len = decode_base64((unsigned char *) psk_base64, strlen(psk_base64), psk);
    if (len == 32 || len == 16) {
      mesh::Utils::sha256(dest->channel.hash, sizeof(dest->channel.hash), psk, len);
      StrHelper::strncpy(dest->name, name, sizeof(dest->name));
      num_channels++;
      return dest;
    }
  }
  return NULL;
}
bool BaseChatMesh::getChannel(int idx, ChannelDetails& dest) {
  if (idx >= 0 && idx < MAX_GROUP_CHANNELS) {
    dest = channels[idx];
    return true;
  }
  return false;
}
bool BaseChatMesh::setChannel(int idx, const ChannelDetails& src) {
  if (idx >= 0 && idx < MAX_GROUP_CHANNELS) {
    // src.channel.hash was already correctly derived from the PSK when the
    // channel was first added (or previously persisted) -- trust it as-is.
    channels[idx] = src;
    return true;
  }
  return false;
}
int BaseChatMesh::findChannelIdx(const mesh::GroupChannel& ch) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (channels[i].channel.hash[0] == ch.hash[0]) return i;
  }
  return -1;
}
#else
ChannelDetails* BaseChatMesh::addChannel(const char* name, const char* psk_base64) {
  return NULL;
}
bool BaseChatMesh::getChannel(int idx, ChannelDetails& dest) {
  return false;
}
bool BaseChatMesh::setChannel(int idx, const ChannelDetails& src) {
  return false;
}
int BaseChatMesh::findChannelIdx(const mesh::GroupChannel& ch) {
  return -1;
}
#endif

bool BaseChatMesh::getContactByIdx(uint32_t idx, ContactInfo& contact) {
  if (idx >= num_contacts) return false;

  contact = contacts[idx];
  return true;
}

ContactsIterator BaseChatMesh::startContactsIterator() {
  return ContactsIterator(MAX_ANON_CONTACTS);
}

bool ContactsIterator::hasNext(const BaseChatMesh* mesh, ContactInfo& dest) {
  if (next_idx >= mesh->getTotalContactSlots()) return false;

  dest = mesh->contacts[next_idx++];
  return true;
}

void BaseChatMesh::loop() {
  Mesh::loop();

  if (txt_send_timeout && millisHasNowPassed(txt_send_timeout)) {
    onSendTimeout();
    txt_send_timeout = 0;
  }

  if (_pendingLoopback) {
    onRecvPacket(_pendingLoopback);
    releasePacket(_pendingLoopback);
    _pendingLoopback = NULL;
  }
}
