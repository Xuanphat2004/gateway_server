/* modbus_rtu_server.c
 * RTU server listens to Redis channel for Modbus JSON requests,
 * parses the request, and performs Modbus RTU transactions.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modbus/modbus.h>
#include <hiredis/hiredis.h>
#include <jansson.h>
#include <unistd.h>

#define SERIAL_PORT "/dev/ttyS1"  // Serial port
#define BAUDRATE 9600
#define REDIS_CHANNEL "modbus_request"
#define REDIS_RESPONSE_CHANNEL "modbus_response"

// Gửi phản hồi lên Redis
void send_response_to_redis(redisContext *redis, int transaction_id, int rtu_id, int success, const uint16_t *data, int length) {
    json_t *response = json_object();
    json_object_set_new(response, "transaction_id", json_integer(transaction_id));
    json_object_set_new(response, "rtu_id", json_integer(rtu_id));
    json_object_set_new(response, "success", json_boolean(success));

    if (success && data != NULL) {
        json_t *array = json_array();
        for (int i = 0; i < length; ++i) {
            json_array_append_new(array, json_integer(data[i]));
        }
        json_object_set_new(response, "data", array);
    }

    char *response_str = json_dumps(response, 0);
    redisCommand(redis, "PUBLISH %s %s", REDIS_RESPONSE_CHANNEL, response_str);

    free(response_str);
    json_decref(response);
}

// Xử lý yêu cầu RTU từ JSON
void process_rtu_request(redisContext *redis, const char *json_str) {
    json_error_t error;
    json_t *root = json_loads(json_str, 0, &error);
    if (!root) {
        fprintf(stderr, "❌ JSON parse error: %s\n", error.text);
        return;
    }

    int transaction_id = json_integer_value(json_object_get(root, "transaction_id"));
    int rtu_id = json_integer_value(json_object_get(root, "rtu_id"));
    int rtu_address = json_integer_value(json_object_get(root, "rtu_address"));
    int function = json_integer_value(json_object_get(root, "function"));
    int quantity = json_integer_value(json_object_get(root, "quantity"));

    printf("Received Modbus command: Transaction ID = %d, RTU ID = %d, Address = %d, Function = %d, Quantity = %d\n",
           transaction_id, rtu_id, rtu_address, function, quantity);

    modbus_t *ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, 'N', 8, 1);
    if (!ctx) {
        fprintf(stderr, "❌ Failed to create Modbus context\n");
        json_decref(root);
        return;
    }

    modbus_set_slave(ctx, rtu_id);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "❌ Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        json_decref(root);
        return;
    }

    uint16_t tab_reg[125];
    int rc = -1;

    if (function == 3) {
        rc = modbus_read_registers(ctx, rtu_address, quantity, tab_reg);
    } else {
        fprintf(stderr, "❌ Unsupported function code\n");
    }

    if (rc == -1) {
        fprintf(stderr, "❌ Modbus read failed: %s\n", modbus_strerror(errno));
        send_response_to_redis(redis, transaction_id, rtu_id, 0, NULL, 0);
    } else {
        printf("Read success: ");
        for (int i = 0; i < rc; ++i) printf("%d ", tab_reg[i]);
        printf("\n");
        send_response_to_redis(redis, transaction_id, rtu_id, 1, tab_reg, rc);
    }

    modbus_close(ctx);
    modbus_free(ctx);
    json_decref(root);
}

int main() {
    redisContext *redis = redisConnect("127.0.0.1", 6379);
    if (redis == NULL || redis->err) {
        printf("Redis connection error\n");
        return 1;
    }
    printf("Connected to Redis\n");

    redisReply *reply = redisCommand(redis, "SUBSCRIBE %s", REDIS_CHANNEL);
    if (reply) freeReplyObject(reply);

    printf("Waiting for JSON requests from Redis...\n");

    while (1) {
        redisReply *msg;
        if (redisGetReply(redis, (void **)&msg) == REDIS_OK && msg) {
            if (msg->type == REDIS_REPLY_ARRAY && msg->elements == 3 &&
                strcmp(msg->element[0]->str, "message") == 0) {
                const char *json_str = msg->element[2]->str;
                printf("Received JSON from Redis: %s\n", json_str);
                process_rtu_request(redis, json_str);
            }
            freeReplyObject(msg);
        } else {
            printf("Redis disconnected\n");
            break;
        }
    }

    redisFree(redis);
    return 0;
}


