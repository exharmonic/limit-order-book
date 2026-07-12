#pragma once
#include "RingBuffer.hpp"
#include "ITCHParser.hpp"

// Ref: https://datatracker.ietf.org/doc/html/rfc791#section-3.1
#pragma pack(push, 1)

struct PcapGlobalHeader {
    uint32_t magicNumber;
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint32_t gmtToLocal;
    uint32_t timestampAccuracy;
    uint32_t snapLen;
    uint32_t linkType;
};
struct PcapRecordHeader {
    uint32_t tsSec;
    uint32_t tsMSec;
    uint32_t capLen;
    uint32_t orgCapLen;
};
struct EthernetHeader {
    uint8_t dstMac[6];
    uint8_t srcMac[6];
    uint16_t etherType;
};
struct IPv4Header {
    uint8_t versionIHL;
    uint8_t typeOfService;
    uint16_t totalLength;
    uint16_t identification;
    uint16_t flagsFragmentOffset;
    uint8_t timeToLive;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t srcIP;
    uint32_t dstIP;
};
struct UDPHeader {
    uint16_t srcPort;
    uint16_t dstPort;
    uint16_t length;
    uint16_t checksum;
};
struct MoldUDP64Header {
    char session[10];
    uint64_t sequenceNumber;
    uint16_t messageCount;
};
#pragma pack(pop)

class   PCAPITCHParser {
    public:
        static void parseAndPush(const char* filepath, RingBuffer<Order, 1048576>& buffer) {
            int fd = open(filepath, O_RDONLY);
            if (fd == -1) {
                std::cerr<<"[SYSTEM] Failed to open the dataset.\n";
                close(fd);
                return;
            }
            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                std::cerr<<"[SYSTEM] Failed to get file size.\n";
                close(fd);
                return;
            }
            size_t length = sb.st_size; // Size of files in bytes
            
            const char* data = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0));
            madvise((void*)data, length, MADV_SEQUENTIAL);

            if (data == MAP_FAILED) {
                std::cerr<<"[NETWORK] Failed to map memory.\n";
                close(fd);
                return;
            }

            const char* ptr = data;
            const char* end = data + length;

            if (static_cast<size_t> (end-ptr) < sizeof(PcapGlobalHeader)) {
                std::cerr << "[PCAP] File too small to contain a pcap global header.\n";
                munmap((void*)data, length);
                close(fd);
                return;
            }

            const auto* gHeader = reinterpret_cast<const PcapGlobalHeader*>(ptr);

            // Now, we check the magicNumber to see if the binary values are written in big-endian or little-endian format. Hence, bswap may or may not be required, based on this.
            bool needSwap;
            if (gHeader->magicNumber == 0xa1b2c3d4u || gHeader->magicNumber == 0xa1b23c4du) {
                needSwap = false;
            } else if (gHeader->magicNumber == 0xd4c3b2a1u || gHeader->magicNumber == 0x4d3cb2a1u) {
                needSwap = true;
            } else {
                std::cerr << "[PCAP] Unrecognized magic number (0x" << std::hex << gHeader->magicNumber
                          << std::dec << "). Not a classic pcap file.\n";
                munmap((void*)data, length);
                close(fd);
                return;
            }

            uint32_t linkType = needSwap ? __builtin_bswap32(gHeader->linkType) : gHeader->linkType;
            if (linkType != 1) { // That is link type is not Ethernet (1)
                std::cerr << "[PCAP] Unsupported link-layer type (" << linkType
                          << "). Only Ethernet (LINKTYPE_ETHERNET = 1) captures are supported.\n";
                munmap((void*)data, length); close(fd); return;
            }

            ptr += sizeof(PcapGlobalHeader);
            uint64_t packetsProcessed = 0, packetsSkipped = 0;

           while (static_cast<size_t>(end - ptr) >= sizeof(PcapRecordHeader)) {
                const auto* rHeader = reinterpret_cast<const PcapRecordHeader*>(ptr);
                ptr += sizeof(PcapRecordHeader);

                uint32_t capLen = needSwap ? __builtin_bswap32(rHeader->capLen) : rHeader->capLen;
                if (ptr + capLen > end) { std::cerr << "[PCAP] Truncated capture -- stopping.\n"; break; }

                const char* packetStart = ptr;
                const char* packetEnd = ptr + capLen;
                ptr += capLen;

                if (!processPacket(packetStart, packetEnd, buffer)) { ++packetsSkipped; continue; }
                ++packetsProcessed;
            }

            std::cout << "[PCAP] Processed " << packetsProcessed << " market-data packets, skipped "
                      << packetsSkipped << " non-matching/malformed packets.\n";
            munmap((void*)data, length);
            close(fd);
        }
    
        private:
        static bool processPacket(const char* p, const char* packetEnd, RingBuffer<Order, 1048576>& buffer) {
            if (p + sizeof(EthernetHeader) > packetEnd) return false;
            const auto* eth = reinterpret_cast<const EthernetHeader*>(p);
            uint16_t etherType = ntohs16(eth->etherType);
            p += sizeof(EthernetHeader);

            if (etherType == 0x8100) { // single 802.1Q VLAN tag
                if (p + 4 > packetEnd) return false;
                etherType = ntohs16(*reinterpret_cast<const uint16_t*>(p + 2));
                p += 4;
            }
            if (etherType != 0x0800) return false;

            if (p + sizeof(IPv4Header) > packetEnd) return false;
            const auto* ip = reinterpret_cast<const IPv4Header*>(p);
            uint8_t version = ip->versionIHL >> 4;
            uint8_t ihl = (ip->versionIHL & 0x0F) * 4;
            if (version != 4 || ihl < 20) return false;
            if (ip->protocol != 17) return false;
            if (p + ihl > packetEnd) return false;
            p += ihl;

            if (p + sizeof(UDPHeader) > packetEnd) return false;
            p += sizeof(UDPHeader);

            if (p + sizeof(MoldUDP64Header) > packetEnd) return false;
            const auto* mold = reinterpret_cast<const MoldUDP64Header*>(p);
            uint16_t messageCount = ntohs16(mold->messageCount);
            p += sizeof(MoldUDP64Header);

            if (messageCount == 0x0000 || messageCount == 0xFFFF) return true; // heartbeat/end-of-session

            for (uint16_t i = 0; i < messageCount; ++i) {
                if (p + 2 > packetEnd) break;
                uint16_t msgLength = ntohs16(*reinterpret_cast<const uint16_t*>(p));
                p += 2;
                if (p + msgLength > packetEnd) break;
                ITCHParser::processMessage(p, buffer);
                p += msgLength;
            }
            return true;
        }

        static inline uint16_t ntohs16(uint16_t v) { return __builtin_bswap16(v); }
};