import struct
import os
import random
import socket

os.makedirs('data', exist_ok=True)

PCAP_MAGIC = 0xa1b2c3d4
LINKTYPE_ETHERNET = 1
MAX_PACKET_BYTES = 1400
MAX_MSGS_PER_PACKET = 20

def eth_header(dst_mac, src_mac, ethertype):
    return dst_mac + src_mac + struct.pack('>H', ethertype)


def ipv4_header(payload_len, src_ip, dst_ip, ident):
    version_ihl = (4 << 4) | 5
    total_len = 20 + payload_len
    return struct.pack('>BBHHHBBH4s4s',
        version_ihl, 0, total_len, ident, 0, 64, 17, 0, socket.inet_aton(src_ip), socket.inet_aton(dst_ip));


def udp_header(payload_len, src_port, dst_port):
    length = 8 + payload_len
    return struct.pack('>HHHH', src_port, dst_port, length, 0)


def moldudp64_header(session, seq_num, msg_count):
    session_bytes = session.encode('ascii').ljust(10, b'\x00')[:10]
    return session_bytes + struct.pack('>QH', seq_num, msg_count)


def build_itch_type_a(order_id, qty, price):
    # Matches itch_generator.py's type-'A' message exactly (always BUY).
    header = struct.pack('>c H H', b'A', 1, order_id % 65536)
    timestamp_48 = struct.pack('>Q', 123456789000000 + order_id)[2:8]
    payload = struct.pack('>Q c I 8s I', order_id, b'B', qty, b'AAPL    ', price)
    return header + timestamp_48 + payload


def build_itch_type_f(order_id, qty, price):
    # Matches itch_generator.py's type-'F' message exactly (always SELL).
    header = struct.pack('>c H H', b'F', 1, order_id % 65536)
    timestamp_48 = struct.pack('>Q', 123456789000000 + order_id)[2:8]
    payload = struct.pack('>Q c I 8s I 4s', order_id, b'S', qty, b'AAPL    ', price, b'MSCO')
    return header + timestamp_48 + payload


def generate_pcap_sample(filename, total_orders=1000000, raw_itch_filename=None, seed=None):
    if seed is not None:
        random.seed(seed)

    dst_mac = bytes.fromhex('0180C2000001')  # arbitrary multicast-style MAC
    src_mac = bytes.fromhex('AABBCCDDEEFF')
    src_ip, dst_ip = '10.0.0.1', '224.0.1.1'
    src_port, dst_port = 30001, 30002

    raw_f = open(raw_itch_filename, 'wb') if raw_itch_filename else None

    with open(filename, 'wb') as f:
        print(f"Generating {total_orders} ITCH orders wrapped in Ethernet/IPv4/UDP/MoldUDP64 to {filename}...")

        # 24-byte pcap global header
        f.write(struct.pack('<IHHiIII', PCAP_MAGIC, 2, 4, 0, 0, 65535, LINKTYPE_ETHERNET))

        half_orders = total_orders // 2
        seq = 1
        ident = 1

        order_id = 1
        while order_id <= total_orders:
            batch = []
            batch_bytes = 0
            while (len(batch) < MAX_MSGS_PER_PACKET
                   and order_id <= total_orders
                   and batch_bytes < MAX_PACKET_BYTES):
                qty = random.randint(1, 500)
                price = random.randint(10000, 90000)

                if order_id <= half_orders:
                    itch_msg = build_itch_type_a(order_id, qty, price)
                else:
                    itch_msg = build_itch_type_f(order_id, qty, price)

                if raw_f:
                    raw_f.write(struct.pack('>H', len(itch_msg)))
                    raw_f.write(itch_msg)

                batch.append(itch_msg)
                batch_bytes += 2 + len(itch_msg)  # 2-byte length prefix + message
                order_id += 1

            mold_payload = moldudp64_header('SESSION001', seq, len(batch))
            for msg in batch:
                mold_payload += struct.pack('>H', len(msg)) + msg
            seq += len(batch)

            udp_payload = udp_header(len(mold_payload), src_port, dst_port) + mold_payload
            ip_payload = ipv4_header(len(udp_payload), src_ip, dst_ip, ident) + udp_payload
            packet = eth_header(dst_mac, src_mac, 0x0800) + ip_payload
            ident += 1

            # 16-byte pcap record header + captured packet bytes
            f.write(struct.pack('<IIII', 0, 0, len(packet), len(packet)))
            f.write(packet)

    if raw_f:
        raw_f.close()

    print(f"Successfully generated {filename}!")
    if raw_itch_filename:
        print(f"Also wrote equivalent raw ITCH stream to {raw_itch_filename} for cross-validation.")


if __name__ == '__main__':
    generate_pcap_sample('data/sample.pcap')