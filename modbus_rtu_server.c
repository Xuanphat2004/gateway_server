#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <hiredis/hiredis.h>
#include <jansson.h>
#include <modbus/modbus.h>

#define MAX_QUEUE 100

//========================= Queue Request: Lưu các gói cần gửi đi =================================
typedef struct
{
    int transaction_id;
    int rtu_id;
    int address;
    int function;
    int quantity;
} RequestPacket;

RequestPacket request_queue[MAX_QUEUE];
int queue_front = 0, queue_rear = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

void add_request(RequestPacket pkt)
{
    pthread_mutex_lock(&queue_mutex);
    request_queue[queue_rear] = pkt;
    queue_rear = (queue_rear + 1) % MAX_QUEUE;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

RequestPacket take_request()
{
    pthread_mutex_lock(&queue_mutex);
    while (queue_front == queue_rear)
    {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    RequestPacket pkt = request_queue[queue_front];
    queue_front = (queue_front + 1) % MAX_QUEUE;
    pthread_mutex_unlock(&queue_mutex);

    return pkt;
}

//========================= Queue Response: Lưu phản hồi từ SmartLogger để gửi lại =================
typedef struct
{
    int transaction_id;
    int status;
    int value;
} ResponsePacket;

ResponsePacket response_queue[MAX_QUEUE];
int resp_front = 0, resp_rear = 0;
pthread_mutex_t resp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t resp_cond = PTHREAD_COND_INITIALIZER;

void add_response(ResponsePacket pkt)
{
    pthread_mutex_lock(&resp_mutex);
    response_queue[resp_rear] = pkt;
    resp_rear = (resp_rear + 1) % MAX_QUEUE;
    pthread_cond_signal(&resp_cond);
    pthread_mutex_unlock(&resp_mutex);
}

ResponsePacket take_response()
{
    pthread_mutex_lock(&resp_mutex);
    while (resp_front == resp_rear)
    {
        pthread_cond_wait(&resp_cond, &resp_mutex);
    }

    ResponsePacket pkt = response_queue[resp_front];
    resp_front = (resp_front + 1) % MAX_QUEUE;
    pthread_mutex_unlock(&resp_mutex);

    return pkt;
}

//======================== Thread 1: Nhận gói lệnh từ Redis -> queue_request =======================
void *ReceiverThread(void *arg)
{
    redisContext *redis = redisConnect("127.0.0.1", 6379);
    redisReply *reply = redisCommand(redis, "SUBSCRIBE modbus_request");
    freeReplyObject(reply);
    printf("[REDIS] Subscribed to modbus_request\n");

    while (1)
    {
        redisReply *msg;
        if (redisGetReply(redis, (void **)&msg) == REDIS_OK && msg)
        {
            if (msg->type == REDIS_REPLY_ARRAY && msg->elements == 3)
            {
                printf("[RTU Server] Received message: %s\n", msg->str);
                const char *json_str = msg->element[2]->str;
                json_error_t error;
                json_t *root = json_loads(json_str, 0, &error);
                if (!root)
                {
                    fprintf(stderr, "[RTU Server] JSON parse error: %s\n", error.text);
                    freeReplyObject(msg);
                    continue;
                }
                RequestPacket req;
                req.transaction_id = json_integer_value(json_object_get(root, "transaction_id"));
                req.rtu_id = json_integer_value(json_object_get(root, "rtu_id"));
                req.address = json_integer_value(json_object_get(root, "rtu_address"));
                req.function = json_integer_value(json_object_get(root, "function"));
                req.quantity = json_integer_value(json_object_get(root, "quantity"));
                add_request(req);
                json_decref(root);
                printf("[RTU Server] Received transaction_id %d, added to queue\n", req.transaction_id);
            }
            freeReplyObject(msg);
        }
    }
    redisFree(redis);
    return NULL;
}

//========================= Thread 2: Gửi lệnh Modbus xuống SmartLogger ===========================
void *SenderThread(void *arg)
{
    modbus_t *ctx = modbus_new_tcp("127.0.0.1", 502); // IP SmartLogger
    if (!ctx || modbus_connect(ctx) == -1)
    {
        fprintf(stderr, "[RTU Server] Modbus connection failed\n");
        return NULL;
    }
    printf("[RTU Server] Connected to SmartLogger\n");

    while (1)
    {
        RequestPacket req = take_request();
        printf("[RTU Server] Processing transaction_id %d\n", req.transaction_id);

        int rc = -1;
        uint16_t value[req.quantity];
        if (req.function == 3)
        { // Read Holding Register
            rc = modbus_read_registers(ctx, req.address, req.quantity, value);
        }
        else if (req.function == 4)
        { // Read Input Register
            rc = modbus_read_input_registers(ctx, req.address, req.quantity, value);
        }
        else
        {
            printf("[RTU Server] Unsupported function: %d\n", req.function);
            rc = -1;
        }

        ResponsePacket resp;
        resp.transaction_id = req.transaction_id;
        if (rc != -1)
        {
            resp.status = 0; // OK
            resp.value = value[0];
            printf("[SENDER] transaction_id %d success, value %d\n", resp.transaction_id, resp.value);
        }
        else
        {
            resp.status = 1; // ERROR
            resp.value = 0;
            printf("[SENDER] transaction_id %d failed\n", resp.transaction_id);
        }
        add_response(resp);
    }

    modbus_close(ctx);
    modbus_free(ctx);
    return NULL;
}

//======================== Thread 3: Gửi phản hồi lên Redis (modbus_response) =====================
void *ResponseThread(void *arg)
{
    redisContext *redis = redisConnect("127.0.0.1", 6379);

    while (1)
    {
        ResponsePacket resp = take_response();

        json_t *root = json_object();
        json_object_set_new(root, "transaction_id", json_integer(resp.transaction_id));
        json_object_set_new(root, "status", json_integer(resp.status));
        json_object_set_new(root, "value", json_integer(resp.value));
        char *json_str = json_dumps(root, 0);

        redisCommand(redis, "PUBLISH modbus_response %s", json_str);
        printf("[RESPONSE] Sent transaction_id %d with status %d and value %d\n", resp.transaction_id, resp.status, resp.value);

        free(json_str);
        json_decref(root);
    }
    redisFree(redis);
    return NULL;
}

//======================== Main: Tạo 3 thread và chạy vĩnh viễn ====================================
int main()
{
    pthread_t recv_t, send_t, resp_t;

    pthread_create(&recv_t, NULL, ReceiverThread, NULL);
    pthread_create(&send_t, NULL, SenderThread, NULL);
    pthread_create(&resp_t, NULL, ResponseThread, NULL);

    pthread_join(recv_t, NULL);
    pthread_join(send_t, NULL);
    pthread_join(resp_t, NULL);

    return 0;
}
