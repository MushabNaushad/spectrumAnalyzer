// ===========================================================================
//  ESP32 + ADS1115 X-Y scope  --  TEST BUILD (Wi-Fi / UDP)
//
//  AIN0 = signal 1, AIN1 = signal 2, AIN2 = sawtooth (X sweep).
//  Streams raw sample sets over UDP. Browsers cannot receive UDP, so run
//  tools/scope_server.py on the PC: it receives the packets and serves the
//  live X-Y page at http://localhost:8000
// ===========================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"
#include "ScopeSampler.h"

// ---------------- packet format (little-endian, packed) ----------------
#pragma pack(push, 1)
struct PacketHeader {
  char     magic[4];        // "ADSX"
  uint8_t  version;         // 1
  uint8_t  state;           // 0 = learning saw max, 1 = locked
  uint16_t count;           // samples in this packet
  uint32_t seq;             // packet counter (loss detection)
  int16_t  sawMax;          // counts: locked max, or running max while learning
  uint16_t setsPerSecX10;   // acquisition rate * 10
  float    lsbVolts;        // volts per count (125e-6 @ +-4.096 V)
};
struct PacketSample {
  int16_t saw, sig1, sig2;  // raw counts
  uint8_t flags;            // SAMPLE_* bits (sweep start / calibrated / error)
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 20, "header layout");
static_assert(sizeof(PacketSample) == 7, "sample layout");

constexpr uint8_t  PKT_VERSION      = 1;
constexpr uint16_t MAX_PER_PACKET   = 10;   // ~45 ms of data @ ~220 sets/s
constexpr uint32_t MAX_PACKET_AGEMS = 30;   // flush even if not full

static ScopeSampler sampler;
static WiFiUDP udp;
static IPAddress target;
static uint8_t  pkt[sizeof(PacketHeader) + MAX_PER_PACKET * sizeof(PacketSample)];
static uint16_t nInPkt = 0;
static uint32_t pktT0 = 0, seq = 0, sentOk = 0, sentFail = 0;

static IPAddress resolveTarget() {
  IPAddress ip;
  if (strlen(UDP_TARGET_IP) > 0 && ip.fromString(UDP_TARGET_IP)) return ip;
  const IPAddress me = WiFi.localIP(), mask = WiFi.subnetMask();
  IPAddress bcast;
  for (int i = 0; i < 4; ++i) bcast[i] = me[i] | uint8_t(~mask[i]);  // subnet broadcast
  return bcast;
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds 100+ ms latency jitter
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] connecting to \"%s\"", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  target = resolveTarget();
  Serial.printf("\n[WiFi] IP %s -> sending UDP to %s:%d\n", WiFi.localIP().toString().c_str(),
                target.toString().c_str(), UDP_PORT);
}

static void flushPacket() {
  if (nInPkt == 0) return;
  PacketHeader h;
  memcpy(h.magic, "ADSX", 4);
  h.version = PKT_VERSION;
  h.state = sampler.calibrated() ? 1 : 0;
  h.count = nInPkt;
  h.seq = seq++;
  h.sawMax = sampler.calibrated() ? sampler.sawMax() : sampler.sawRunningMax();
  h.setsPerSecX10 = uint16_t(sampler.setsPerSec() * 10.0f + 0.5f);
  h.lsbVolts = sampler.lsbVolts();
  memcpy(pkt, &h, sizeof h);

  const size_t len = sizeof h + nInPkt * sizeof(PacketSample);
  const bool ok = WiFi.status() == WL_CONNECTED && udp.beginPacket(target, UDP_PORT) &&
                  udp.write(pkt, len) == len && udp.endPacket();
  if (ok) ++sentOk; else ++sentFail;
  nInPkt = 0;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ADS1115 X-Y scope : UDP test build ===");
  connectWiFi();

  ScopeSamplerConfig cfg;
  cfg.sdaPin = ADS_SDA_PIN;
  cfg.sclPin = ADS_SCL_PIN;
  cfg.i2cAddr = ADS_I2C_ADDR;
  cfg.i2cHz = ADS_I2C_HZ;
  cfg.chSig1 = CH_SIG1;
  cfg.chSig2 = CH_SIG2;
  cfg.chSaw = CH_SAW;
  cfg.recalPin = RECAL_PIN;
  cfg.core = 0;  // ADC on core 0 (low prio, Wi-Fi preempts it - harmless)
  if (!sampler.begin(cfg)) Serial.println("[ERR] sampler task failed to start");
}

void loop() {
  ScopeSample s;
  if (sampler.pop(s, pdMS_TO_TICKS(5))) {
    do {
      if (nInPkt == 0) pktT0 = millis();
      const PacketSample p{s.saw, s.sig1, s.sig2, s.flags};
      memcpy(pkt + sizeof(PacketHeader) + nInPkt * sizeof(PacketSample), &p, sizeof p);
      if (++nInPkt >= MAX_PER_PACKET) flushPacket();
    } while (sampler.pop(s, 0));
  }
  if (nInPkt && millis() - pktT0 >= MAX_PACKET_AGEMS) flushPacket();

  static uint32_t tStat = 0;
  if (millis() - tStat >= 2000) {
    tStat = millis();
    if (WiFi.status() == WL_CONNECTED) target = resolveTarget();  // IP may change on reconnect
    Serial.printf("[STAT] %s  saw max %.3f V  %.1f sets/s  pkts ok %lu fail %lu  "
                  "dropped %lu  adc err %lu\n",
                  sampler.calibrated() ? "LOCKED" : "LEARNING",
                  (sampler.calibrated() ? sampler.sawMax() : sampler.sawRunningMax()) *
                      sampler.lsbVolts(),
                  sampler.setsPerSec(), (unsigned long)sentOk, (unsigned long)sentFail,
                  (unsigned long)sampler.dropped(), (unsigned long)sampler.adcErrors());
  }
}
