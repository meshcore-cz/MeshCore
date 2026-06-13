#pragma once

#include "helpers/AbstractBridge.h"

#ifdef WITH_BLEEDGE_BRIDGE

#include "helpers/bleedge/BLEEdgeWire.h"
#include "helpers/SimpleMeshTables.h"

#ifndef NRF52_PLATFORM
#error "BLEEdgeBridge currently targets nRF52/Bluefruit builds only"
#endif

#include <bluefruit.h>
#include <vector>

struct BLEEdgeBridgeStatus {
  bool running;
  bool connected;
  uint32_t txPackets;
  uint32_t rxPackets;
  uint32_t txDatagrams;
  uint32_t rxDatagrams;
  uint32_t forwardedDatagrams;
  uint32_t announceSeq;
  uint32_t lastTxMs;
  uint32_t lastRxMs;
  bool scanning;
  bool centralConnected;
  bool peripheralConnected;
};

class BLEEdgeBridge : public AbstractBridge {
public:
  BLEEdgeBridge(mesh::PacketManager* mgr, mesh::RTCClock* rtc, const mesh::LocalIdentity* identity,
                uint16_t inboundDelayMs = 500);

  void begin() override;
  void end() override;
  bool isRunning() const override;
  void loop() override;
  void sendPacket(mesh::Packet* packet) override;
  void onPacketReceived(mesh::Packet* packet) override;
  void setNodeName(const char* name);
  void getStatus(BLEEdgeBridgeStatus& status) const;
  bool sendAnnounceNow();

private:
  static constexpr uint16_t BLEEDGE_CAPS =
      bleedge::CAP_SENDER | bleedge::CAP_RECEIVER | bleedge::CAP_RELAY | bleedge::CAP_GATEWAY;
  static constexpr size_t FRAME_MTU = 200;
  static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 15000;

  static BLEEdgeBridge* _instance;
  static void onConnect(uint16_t conn_handle);
  static void onDisconnect(uint16_t conn_handle, uint8_t reason);
  static void onCentralConnect(uint16_t conn_handle);
  static void onCentralDisconnect(uint16_t conn_handle, uint8_t reason);
  static void onScan(ble_gap_evt_adv_report_t* report);
  static void onPacketInWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
  static void onClientPacketOutNotify(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);

  const mesh::LocalIdentity* _identity;
  BLEService _service;
  BLECharacteristic _nodeInfo;
  BLECharacteristic _packetIn;
  BLECharacteristic _packetOut;
  BLEClientService _clientService;
  BLEClientCharacteristic _clientPacketIn;
  BLEClientCharacteristic _clientPacketOut;
  SimpleMeshTables _seen_packets;
  bleedge::DedupCache _dedup;
  bleedge::Reassembler _reassembler;
  mesh::PacketManager* _mgr;
  mesh::RTCClock* _rtc;
  uint16_t _conn_handle;
  uint16_t _central_conn_handle;
  uint16_t _inbound_delay_ms;
  uint32_t _last_announce;
  uint32_t _announce_seq;
  uint32_t _epoch;
  uint32_t _tx_packets;
  uint32_t _rx_packets;
  uint32_t _tx_datagrams;
  uint32_t _rx_datagrams;
  uint32_t _forwarded_datagrams;
  uint32_t _last_tx_ms;
  uint32_t _last_rx_ms;
  bool _initialized;
  bool _scanner_started;
  bool _central_ready;
  char _node_name[32];

  void nodeId(uint8_t out[bleedge::NODE_ID_LEN]) const;
  void randomBytes(uint8_t* out, size_t len);
  void sendDatagram(const std::vector<uint8_t>& dg);
  void sendAnnounce();
  void handleDatagram(const std::vector<uint8_t>& dg, uint16_t sender);
  void beginScanner();
  bool isSelfAdvert(const ble_gap_evt_adv_report_t* report) const;
  void handleCentralConnect(uint16_t conn_handle);
  bool isConnected() const;
};

#endif
