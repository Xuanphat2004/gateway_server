from flask import Flask, render_template, request, redirect
import sqlite3
from database_service import add_mapping, delete_mapping

app = Flask(__name__)

@app.route('/')
def index():
    """Hiển thị trang cấu hình ánh xạ"""
    conn = sqlite3.connect('modbus_mapping.db')
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM mapping")
    rows = cursor.fetchall()
    mappings = [{'tcp_address': row[0], 'rtu_id': row[1], 'rtu_address': row[2]} for row in rows]
    conn.close()
    return render_template('templates_index.html', mappings=mappings)

@app.route('/add_mapping', methods=['POST'])
def add_mapping_route():
    """Xử lý yêu cầu thêm ánh xạ từ form"""
    tcp_address = int(request.form['tcp_address'])
    rtu_id = int(request.form['rtu_id'])
    rtu_address = int(request.form['rtu_address'])
    add_mapping(tcp_address, rtu_id, rtu_address)
    return redirect('/')

@app.route('/delete_mapping/<int:tcp_address>', methods=['POST'])
def delete_mapping_route(tcp_address):
    """Xử lý yêu cầu xóa ánh xạ dựa trên tcp_address"""
    delete_mapping(tcp_address)
    return redirect('/')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, debug=True)

