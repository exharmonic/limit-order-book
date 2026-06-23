import random
import os

os.makedirs('data', exist_ok=True)

def generate_csv_sample(filename, total_orders=1000000):
    with open(filename, 'w') as f:
        print(f"Generating {total_orders} CSV payloads to {filename}...")
        f.write("orderID,price,quantity,side\n")
        
        half_orders = total_orders // 2

        for i in range(1, half_orders + 1):
            price = random.randint(10000, 90000)
            qty = random.randint(1, 500)
            side = 0 
            f.write(f"{i},{price},{qty},{side}\n")

        for i in range(half_orders + 1, total_orders + 1):
            price = random.randint(10000, 90000)
            qty = random.randint(1, 500)
            side = 1 
            f.write(f"{i},{price},{qty},{side}\n")

    print(f"Successfully generated {filename}!")

if __name__ == '__main__':
    generate_csv_sample('data/orders.csv')