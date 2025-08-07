import socket
import struct

HOST = '127.0.0.1'
PORT = 1502

# Dữ liệu Modbus TCP giả lập
transaction_id = 1
protocol_id = 0
length = 6  # Số lượng byte của phần dữ liệu
rtu_id = 10
address = 10050
function = 3
quantity = 10


# Đóng gói thành 6 trường 2 byte (big-endian)
packet = struct.pack('!7H', transaction_id, protocol_id, length, rtu_id, address, function, quantity)

try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        print("[Client] Kết nối thành công đến server tại {}:{}".format(HOST, PORT))

        s.sendall(packet)
        print("[Client] Đã gửi gói tin Modbus TCP")

        response = s.recv(1024)
        print("[Client] Phản hồi từ server (raw):", response)

        # Nếu phản hồi có dữ liệu, hiển thị dạng số nguyên
        if response:
            response_values = list(response)
            print("[Client] Phản hồi (dạng số nguyên):", response_values)

except ConnectionRefusedError:
    print("[Client] Không thể kết nối tới server tại {}:{}".format(HOST, PORT))

except Exception as e:
    print("[Client] Lỗi xảy ra:", str(e))  