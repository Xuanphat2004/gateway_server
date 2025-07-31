#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <modbus/modbus.h> // Thư viện modbus RTU

int main() {
    // Tạo đối tượng kết nối Modbus RTU
    // /dev/ttyS1: cổng serial RS485 của UC2101-LX
    // 9600: tốc độ baud
    // 'N': không parity
    // 8: 8 bit dữ liệu
    // 1: 1 stop bit
    modbus_t *ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        printf("Không thể tạo đối tượng Modbus\n");
        return 1;
    }

    // Đặt địa chỉ thiết bị slave (PM710 thường là địa chỉ 1)
    modbus_set_slave(ctx, 10);

    // Kết nối với thiết bị
    if (modbus_connect(ctx) == -1) {
        printf("Không kết nối được Modbus: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return 1;
    }

    // Đọc 2 thanh ghi bắt đầu từ địa chỉ 0x0000 (điện áp L-N của PM710)
    uint16_t reg[2]; // Mỗi thanh ghi là 16-bit, đọc 2 cái cho kiểu float

    int result = modbus_read_registers(ctx, 0x1000, 2, reg);
    if (result == -1) {
        printf("Lỗi đọc thanh ghi: %s\n", modbus_strerror(errno));
        modbus_close(ctx);
        modbus_free(ctx);
        return 1;
    }

    // Chuyển 2 thanh ghi sang float (cần đảo thứ tự nếu thiết bị lưu theo kiểu big-endian)
    union {
        uint16_t reg16[2];
        float value;
    } data;

    data.reg16[0] = reg[1]; // thường cần đảo thứ tự nếu PM710 lưu kiểu big-endian
    data.reg16[1] = reg[0];

    // In kết quả
    printf("Điện áp L-N (pha A): %.2f V\n", data.value);

    // Đóng kết nối và giải phóng bộ nhớ
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}

