import struct
import os
import random

os.makedirs('data', exist_ok=True)

def generate_itch_sample(filename, total_orders=1000000):
    with open(filename, 'wb') as f:
        print(f"Generating {total_orders} binary ITCH 5.0 payloads to {filename}...")
        
        half_orders = total_orders // 2

        for i in range(1, half_orders + 1):
            f.write(struct.pack('>H', 36)) 
            timestamp_48 = struct.pack('>Q', 123456789000000 + i)[2:8]
            
            tracking_number = i % 65536 
            header = struct.pack('>c H H', b'A', 1, tracking_number)
            
            qty = random.randint(1, 500)
            price = random.randint(10000, 90000)
            
            payload = struct.pack('>Q c I 8s I', i, b'B', qty, b'AAPL    ', price)
            
            f.write(header + timestamp_48 + payload)

        for i in range(half_orders + 1, total_orders + 1):
            f.write(struct.pack('>H', 40)) 
            timestamp_48 = struct.pack('>Q', 123456789000000 + i)[2:8]
            
            tracking_number = i % 65536
            header = struct.pack('>c H H', b'F', 1, tracking_number)
            
            qty = random.randint(1, 500)
            price = random.randint(10000, 90000)
            
            payload = struct.pack('>Q c I 8s I 4s', i, b'S', qty, b'AAPL    ', price, b'MSCO')
            
            f.write(header + timestamp_48 + payload)

    print(f"Successfully generated {filename}! Size: ~38 MB.")

if __name__ == '__main__':
    generate_itch_sample('data/sample.itch')