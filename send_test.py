import socket
import struct
import signal
import sys

HOST = '127.0.0.1'
PORT = 1502

transaction_id = 1
protocol_id = 0
length = 6  # Số lượng byte của phần dữ liệu
rtu_id = 3
address = 40100
function = 3
quantity = 10

# Đóng gói thành 7 trường 2 byte (big-endian)
packet = struct.pack('!7H', transaction_id, protocol_id, length, rtu_id, address, function, quantity)

# Biến toàn cục lưu socket
sock = None


try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print("[Client] Kết nối thành công đến server tại {}:{}".format(HOST, PORT))

    sock.sendall(packet)
    print("[Client] Đã gửi gói tin Modbus TCP")

    response = sock.recv(1024)
    print("[Client] Phản hồi từ server (raw):", response)

    if response:
        response_values = list(response)
        print("[Client] Phản hồi :", response_values)

except ConnectionRefusedError:
    print("[Client] Không thể kết nối tới server tại {}:{}".format(HOST, PORT))

except Exception as e:
    print("[Client] Lỗi xảy ra:", str(e))

finally:
    if sock:
        sock.close()
        print("[Client] Socket đã đóng.")
