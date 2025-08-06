import socket
import struct

# Dữ liệu mẫu
transaction_id = 0x0A
tcp_id = 0xA
tcp_address = 0x100
function = 0x03
quantity = 0x0A
dummy = 0xFFFF   # thêm 1 trường 2 byte cho đủ 12 byte

# Đóng gói: 6 số 16-bit (unsigned short), thứ tự big-endian
packet = struct.pack('!6H', transaction_id, tcp_id, tcp_address, function, quantity, dummy)

# Gửi gói tin
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 1502))
sock.sendall(packet)
sock.close()
