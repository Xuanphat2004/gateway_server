import sqlite3
import logging

# Cấu hình logging vào file
logging.basicConfig(filename='database_service.log', level=logging.INFO,
                    format='%(asctime)s %(message)s')

def init_db():
    """Khởi tạo database với bảng mapping và logs"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute('''CREATE TABLE IF NOT EXISTS mapping 
                     (tcp_address INTEGER PRIMARY KEY, rtu_id INTEGER, rtu_address INTEGER)''')
    cursor.execute('''CREATE TABLE IF NOT EXISTS logs 
                     (timestamp TEXT, service TEXT, message TEXT)''')
    conn.commit()
    conn.close()

def add_mapping(tcp_address, rtu_id, rtu_address):
    """Thêm hoặc cập nhật ánh xạ"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute("INSERT OR REPLACE INTO mapping VALUES (?, ?, ?)", 
                   (tcp_address, rtu_id, rtu_address))
    conn.commit()
    conn.close()
    logging.info("Added mapping: TCP {} -> RTU ID {}, Address {}".format(
        tcp_address, rtu_id, rtu_address))

def get_mapping(tcp_address):
    """Lấy thông tin ánh xạ theo địa chỉ TCP"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute("SELECT rtu_id, rtu_address FROM mapping WHERE tcp_address = ?", (tcp_address,))
    result = cursor.fetchone()
    conn.close()
    return result  # (rtu_id, rtu_address) hoặc None

def delete_mapping(tcp_address):
    """Xóa một ánh xạ khỏi database dựa trên tcp_address"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute("DELETE FROM mapping WHERE tcp_address = ?", (tcp_address,))
    conn.commit()
    conn.close()

def add_log(service, message):
    """Thêm log giao tiếp"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute("INSERT INTO logs VALUES (datetime('now', 'localtime'), ?, ?)", (service, message))
    conn.commit()
    conn.close()
    logging.info("Log [{}]: {}".format(service, message))

if __name__ == "__main__":
    init_db()

