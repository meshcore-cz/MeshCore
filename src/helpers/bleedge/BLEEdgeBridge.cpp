#include "helpers/bleedge/BLEEdgeBridge.h"

#ifdef WITH_BLEEDGE_BRIDGE

#include <string.h>
#include <nrf_soc.h>

static const uint8_t BLEEDGE_SERVICE_UUID[16] = {
  0x01, 0xa0, 0xf3, 0x1a, 0x6e, 0x11, 0x8b, 0xa3, 0x19, 0x4c, 0x91, 0x7d, 0x10, 0x6a, 0x7e, 0x9b
};
static const uint8_t BLEEDGE_NODEINFO_UUID[16] = {
  0x02, 0xa0, 0xf3, 0x1a, 0x6e, 0x11, 0x8b, 0xa3, 0x19, 0x4c, 0x91, 0x7d, 0x10, 0x6a, 0x7e, 0x9b
};
static const uint8_t BLEEDGE_PACKETIN_UUID[16] = {
  0x03, 0xa0, 0xf3, 0x1a, 0x6e, 0x11, 0x8b, 0xa3, 0x19, 0x4c, 0x91, 0x7d, 0x10, 0x6a, 0x7e, 0x9b
};
static const uint8_t BLEEDGE_PACKETOUT_UUID[16] = {
  0x04, 0xa0, 0xf3, 0x1a, 0x6e, 0x11, 0x8b, 0xa3, 0x19, 0x4c, 0x91, 0x7d, 0x10, 0x6a, 0x7e, 0x9b
};

BLEEdgeBridge* BLEEdgeBridge::_instance = nullptr;

BLEEdgeBridge::BLEEdgeBridge(mesh::PacketManager* mgr, mesh::RTCClock* rtc,
                             const mesh::LocalIdentity* identity, uint16_t inboundDelayMs)
    : _identity(identity),
      _service(BLEEDGE_SERVICE_UUID),
      _nodeInfo(BLEEDGE_NODEINFO_UUID),
      _packetIn(BLEEDGE_PACKETIN_UUID),
      _packetOut(BLEEDGE_PACKETOUT_UUID),
      _clientService(BLEEDGE_SERVICE_UUID),
      _clientPacketIn(BLEEDGE_PACKETIN_UUID),
      _clientPacketOut(BLEEDGE_PACKETOUT_UUID),
      _mgr(mgr),
      _rtc(rtc),
      _conn_handle(BLE_CONN_HANDLE_INVALID),
      _central_conn_handle(BLE_CONN_HANDLE_INVALID),
      _inbound_delay_ms(inboundDelayMs),
      _last_announce(0),
      _announce_seq(0),
      _epoch(1),
      _tx_packets(0),
      _rx_packets(0),
      _tx_datagrams(0),
      _rx_datagrams(0),
      _forwarded_datagrams(0),
      _last_tx_ms(0),
      _last_rx_ms(0),
      _initialized(false),
      _scanner_started(false),
      _central_ready(false) {
  _instance = this;
  strcpy(_node_name, "MeshCore");
}

void BLEEdgeBridge::nodeId(uint8_t out[bleedge::NODE_ID_LEN]) const {
  memcpy(out, _identity->pub_key, bleedge::NODE_ID_LEN);
}

void BLEEdgeBridge::randomBytes(uint8_t* out, size_t len) {
  if (sd_rand_application_vector_get(out, len) != NRF_SUCCESS) {
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)random(0, 256);
  }
}

void BLEEdgeBridge::begin() {
  if (_initialized) return;

#ifdef BLE_PIN_CODE
  bool restartAdvertising = Bluefruit.Advertising.isRunning();
  if (restartAdvertising) {
    Bluefruit.Advertising.stop();
  }
#else
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configCentralBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);
  Bluefruit.setTxPower(4);
  Bluefruit.setName("BLEEdge");
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
#endif

  Bluefruit.Central.setConnectCallback(onCentralConnect);
  Bluefruit.Central.setDisconnectCallback(onCentralDisconnect);

  _clientService.begin();
  _clientPacketIn.begin(&_clientService);
  _clientPacketOut.setNotifyCallback(onClientPacketOutNotify);
  _clientPacketOut.begin(&_clientService);

  _service.begin();

  std::vector<uint8_t> nodeInfo;
  bleedge::buildNodeInfo(_identity->pub_key, BLEEDGE_CAPS, nodeInfo);
  _nodeInfo.setProperties(CHR_PROPS_READ);
  _nodeInfo.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  _nodeInfo.setFixedLen(nodeInfo.size());
  _nodeInfo.begin();
  _nodeInfo.write(nodeInfo.data(), nodeInfo.size());

  _packetIn.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  _packetIn.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  _packetIn.setWriteCallback(onPacketInWrite);
  _packetIn.begin();

  _packetOut.setProperties(CHR_PROPS_NOTIFY);
  _packetOut.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  _packetOut.setMaxLen(FRAME_MTU);
  _packetOut.begin();

  uint8_t id[bleedge::NODE_ID_LEN];
  nodeId(id);
  uint8_t mfg[2 + bleedge::NODE_ID_LEN];
  mfg[0] = 0xED;
  mfg[1] = 0xBE;
  memcpy(mfg + 2, id, sizeof(id));

#ifdef BLE_PIN_CODE
  Bluefruit.Advertising.addService(_service);
  Bluefruit.ScanResponse.addManufacturerData(mfg, sizeof(mfg));
  if (restartAdvertising) {
    Bluefruit.Advertising.start(0);
  }
#else
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(_service);
  Bluefruit.ScanResponse.addManufacturerData(mfg, sizeof(mfg));
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
#endif

  _last_announce = 0;
  _initialized = true;
  beginScanner();
}

void BLEEdgeBridge::end() {
  if (!_initialized) return;
  Bluefruit.Scanner.stop();
#ifdef BLE_PIN_CODE
  if (_central_conn_handle != BLE_CONN_HANDLE_INVALID) {
    sd_ble_gap_disconnect(_central_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  }
#else
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  if (_conn_handle != BLE_CONN_HANDLE_INVALID) {
    sd_ble_gap_disconnect(_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  }
  if (_central_conn_handle != BLE_CONN_HANDLE_INVALID) {
    sd_ble_gap_disconnect(_central_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  }
#endif
  _conn_handle = BLE_CONN_HANDLE_INVALID;
  _central_conn_handle = BLE_CONN_HANDLE_INVALID;
  _scanner_started = false;
  _central_ready = false;
  _initialized = false;
}

bool BLEEdgeBridge::isRunning() const {
  return _initialized;
}

void BLEEdgeBridge::getStatus(BLEEdgeBridgeStatus& status) const {
  status.running = _initialized;
  status.connected = isConnected();
  status.txPackets = _tx_packets;
  status.rxPackets = _rx_packets;
  status.txDatagrams = _tx_datagrams;
  status.rxDatagrams = _rx_datagrams;
  status.forwardedDatagrams = _forwarded_datagrams;
  status.announceSeq = _announce_seq;
  status.lastTxMs = _last_tx_ms;
  status.lastRxMs = _last_rx_ms;
  status.scanning = _scanner_started && Bluefruit.Scanner.isRunning();
  status.centralConnected = _central_conn_handle != BLE_CONN_HANDLE_INVALID &&
                            Bluefruit.Central.connected(_central_conn_handle);
  status.peripheralConnected = _conn_handle != BLE_CONN_HANDLE_INVALID &&
                               Bluefruit.Periph.connected(_conn_handle);
}

void BLEEdgeBridge::setNodeName(const char* name) {
  if (!name || !*name) {
    strcpy(_node_name, "MeshCore");
  } else {
    strncpy(_node_name, name, sizeof(_node_name) - 1);
    _node_name[sizeof(_node_name) - 1] = 0;
  }
}

void BLEEdgeBridge::loop() {
  if (!_initialized) return;
  _reassembler.reap();
  if (_central_conn_handle == BLE_CONN_HANDLE_INVALID && !_scanner_started) {
    beginScanner();
  }
  if (isConnected() && (millis() - _last_announce >= ANNOUNCE_INTERVAL_MS)) {
    _last_announce = millis();
    sendAnnounce();
  }
}

bool BLEEdgeBridge::isConnected() const {
  bool periph = false;
  if (_conn_handle != BLE_CONN_HANDLE_INVALID) {
    BLEConnection* conn = Bluefruit.Connection(_conn_handle);
    periph = conn != nullptr && conn->connected();
  }
  bool central = _central_conn_handle != BLE_CONN_HANDLE_INVALID &&
                 Bluefruit.Central.connected(_central_conn_handle) && _central_ready;
  return periph || central;
}

void BLEEdgeBridge::sendDatagram(const std::vector<uint8_t>& dg) {
  if (!isConnected()) return;

  std::vector<std::vector<uint8_t>> frames;
  bleedge::fragment(dg.data(), dg.size(), FRAME_MTU, frames);
  for (const auto& frame : frames) {
    if (_conn_handle != BLE_CONN_HANDLE_INVALID && Bluefruit.Periph.connected(_conn_handle)) {
      _packetOut.notify(_conn_handle, frame.data(), frame.size());
    }
    if (_central_ready && _central_conn_handle != BLE_CONN_HANDLE_INVALID &&
        Bluefruit.Central.connected(_central_conn_handle)) {
      _clientPacketIn.write(frame.data(), frame.size());
    }
  }
  _tx_datagrams++;
  _last_tx_ms = millis();
}

void BLEEdgeBridge::beginScanner() {
  if (_scanner_started || Bluefruit.Central.connected()) return;
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80);
  Bluefruit.Scanner.filterUuid(_clientService.uuid);
  Bluefruit.Scanner.useActiveScan(true);
  _scanner_started = Bluefruit.Scanner.start(0);
}

bool BLEEdgeBridge::isSelfAdvert(const ble_gap_evt_adv_report_t* report) const {
  uint8_t mfg[16];
  uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA,
                                                    mfg, sizeof(mfg));
  if (len < 2 + bleedge::NODE_ID_LEN || mfg[0] != 0xED || mfg[1] != 0xBE) return false;
  uint8_t self[bleedge::NODE_ID_LEN];
  nodeId(self);
  return memcmp(mfg + 2, self, sizeof(self)) == 0;
}

void BLEEdgeBridge::handleCentralConnect(uint16_t conn_handle) {
  _central_conn_handle = conn_handle;
  _central_ready = false;

  if (!_clientService.discover(conn_handle) ||
      !_clientPacketIn.discover() ||
      !_clientPacketOut.discover() ||
      !_clientPacketOut.enableNotify()) {
    sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    return;
  }

  _central_ready = true;
  _last_announce = 0;
}

void BLEEdgeBridge::sendAnnounce() {
  uint8_t id[bleedge::DATAGRAM_ID_LEN];
  randomBytes(id, sizeof(id));

  uint8_t self[bleedge::NODE_ID_LEN];
  nodeId(self);
  uint32_t seq = ++_announce_seq;
  int64_t ts = (int64_t)_rtc->getCurrentTime();

  std::vector<uint8_t> signedMsg;
  bleedge::announceSignedMessage(_identity->pub_key, _epoch, seq, ts, BLEEDGE_CAPS,
                                 nullptr, 0, _node_name, "", "meshcore-wio-l1", signedMsg);
  uint8_t sig[bleedge::SIG_LEN];
  _identity->sign(sig, signedMsg.data(), signedMsg.size());

  std::vector<uint8_t> dg;
  bleedge::buildAnnounce(self, BLEEDGE_CAPS, _epoch, seq, ts, id, nullptr, 0,
                         _identity->pub_key, sig, _node_name, "", "meshcore-wio-l1", dg);
  _dedup.seenOrAdd(id);
  sendDatagram(dg);
}

bool BLEEdgeBridge::sendAnnounceNow() {
  if (!_initialized || !isConnected()) return false;
  sendAnnounce();
  return true;
}

void BLEEdgeBridge::sendPacket(mesh::Packet* packet) {
  if (!_initialized || !packet) {
    return;
  }
  if (_seen_packets.hasSeen(packet)) {
    return;
  }

  uint8_t raw[MAX_TRANS_UNIT + 1];
  uint8_t rawLen = packet->writeTo(raw);
  uint8_t self[bleedge::NODE_ID_LEN];
  uint8_t id[bleedge::DATAGRAM_ID_LEN];
  nodeId(self);
  randomBytes(id, sizeof(id));

  std::vector<uint8_t> dg;
  bleedge::buildBroadcastDatagram(self, id, bleedge::MAX_TTL, bleedge::PROTO_MESHCORE_PACKET,
                                  raw, rawLen, dg);
  _dedup.seenOrAdd(id);
  sendDatagram(dg);
  _tx_packets++;
}

void BLEEdgeBridge::handleDatagram(const std::vector<uint8_t>& dg, uint16_t sender) {
  uint8_t self[bleedge::NODE_ID_LEN];
  nodeId(self);
  bleedge::DatagramHeader h = bleedge::parseDatagram(dg.data(), dg.size(), self);
  if (!h.ok || _dedup.seenOrAdd(h.id) || h.pathContainsSelf) return;
  _rx_datagrams++;
  _last_rx_ms = millis();

  if (h.protocol == bleedge::PROTO_MESHCORE_PACKET && h.payload && h.payloadLen > 0) {
    mesh::Packet* pkt = _mgr->allocNew();
    if (!pkt) return;
    if (h.payloadLen <= MAX_TRANS_UNIT + 1 && pkt->readFrom(h.payload, h.payloadLen)) {
      onPacketReceived(pkt);
      _rx_packets++;
    } else {
      _mgr->free(pkt);
    }
  }

  if (!h.sourceRouted && h.ttl > 1) {
    std::vector<uint8_t> fwd;
    if (bleedge::buildFloodForward(dg.data(), dg.size(), self, h.ttl - 1, fwd)) {
      sendDatagram(fwd);
      _forwarded_datagrams++;
    }
  }
}

void BLEEdgeBridge::onPacketReceived(mesh::Packet* packet) {
  if (!_initialized) {
    _mgr->free(packet);
    return;
  }
  if (!_seen_packets.hasSeen(packet)) {
    _mgr->queueInbound(packet, millis() + _inbound_delay_ms);
  } else {
    _mgr->free(packet);
  }
}

void BLEEdgeBridge::onConnect(uint16_t conn_handle) {
  if (!_instance) return;
  _instance->_conn_handle = conn_handle;
  _instance->_last_announce = 0;
}

void BLEEdgeBridge::onDisconnect(uint16_t conn_handle, uint8_t reason) {
  (void)reason;
  if (!_instance) return;
  if (_instance->_conn_handle == conn_handle) {
    _instance->_conn_handle = BLE_CONN_HANDLE_INVALID;
  }
}

void BLEEdgeBridge::onCentralConnect(uint16_t conn_handle) {
  if (!_instance) return;
  _instance->_scanner_started = false;
  _instance->handleCentralConnect(conn_handle);
}

void BLEEdgeBridge::onCentralDisconnect(uint16_t conn_handle, uint8_t reason) {
  (void)reason;
  if (!_instance) return;
  if (_instance->_central_conn_handle == conn_handle) {
    _instance->_central_conn_handle = BLE_CONN_HANDLE_INVALID;
    _instance->_central_ready = false;
    _instance->_scanner_started = false;
  }
  if (_instance->_initialized) {
    _instance->beginScanner();
  }
}

void BLEEdgeBridge::onScan(ble_gap_evt_adv_report_t* report) {
  if (!_instance) {
    Bluefruit.Scanner.resume();
    return;
  }
  if (_instance->_central_conn_handle != BLE_CONN_HANDLE_INVALID ||
      Bluefruit.Central.connected() ||
      _instance->isSelfAdvert(report)) {
    Bluefruit.Scanner.resume();
    return;
  }
  if (!Bluefruit.Central.connect(report)) {
    Bluefruit.Scanner.resume();
  }
}

void BLEEdgeBridge::onPacketInWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  (void)chr;
  if (!_instance || !_instance->_initialized) return;
  if (_instance->_conn_handle == BLE_CONN_HANDLE_INVALID) {
    _instance->_conn_handle = conn_handle;
  }
  if (conn_handle != _instance->_conn_handle) return;
  std::vector<uint8_t> dg;
  if (_instance->_reassembler.addFrame(conn_handle, data, len, dg)) {
    _instance->handleDatagram(dg, conn_handle);
  }
}

void BLEEdgeBridge::onClientPacketOutNotify(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  if (!_instance || !_instance->_initialized || !chr) return;
  uint16_t conn_handle = chr->connHandle();
  std::vector<uint8_t> dg;
  if (_instance->_reassembler.addFrame(conn_handle, data, len, dg)) {
    _instance->handleDatagram(dg, conn_handle);
  }
}

#endif
