#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <hiredis/hiredis.h>
#include <jansson.h>
#include <modbus/modbus.h>
#include <errno.h>


#define MAX_QUEUE 100
#define DEVICE_ADDRESS "127.0.0.1"
#define PORT_DEVICE 1502
#define BUFFER_SIZE 256

#define USE_MODBUS  1               // 1 for RTU Modbus, 0 for TCP Modbus
#define SERIAL_PORT "/dev/ttyUSB0"
#define BAUDRATE    9600
#define PARITY      'N'
#define DATA_BITS   8
#define STOP_BITS   1

//=============================================================================================================================
//========================= structure for request packet receive from TCP server ==============================================
typedef struct 
{
    int transaction_id;
    int protocol_id;
    int lenth;
    int rtu_id;
    int address;
    int function;
    int quantity;
} 
RequestPacket;

RequestPacket request_queue[MAX_QUEUE];

int queue_front = 0; 
int queue_rear = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;


//====================================================================================================
//========================= Function: add request to queue ==========================================
void add_request(RequestPacket add_req) 
{
    pthread_mutex_lock(&queue_mutex);

    request_queue[queue_rear] = add_req;
    queue_rear = (queue_rear + 1) % MAX_QUEUE;

    pthread_cond_signal(&queue_cond);

    pthread_mutex_unlock(&queue_mutex);
}


//====================================================================================================
//========================= Function: take request from queue ========================================
RequestPacket take_request() 
{
    pthread_mutex_lock(&queue_mutex);

    while (queue_front == queue_rear)
    {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    RequestPacket take_request = request_queue[queue_front];
    queue_front = (queue_front + 1) % MAX_QUEUE;

    pthread_mutex_unlock(&queue_mutex);

    return take_request;
}


//======================================================================================================
//========================= structure packet save response from Modbus device ===========================
typedef struct 
{
    int transaction_id;
    int status;
    int value;
} 
ResponsePacket;

ResponsePacket response_queue[MAX_QUEUE];

int resp_front = 0;
int resp_rear = 0;

pthread_mutex_t resp_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t resp_cond = PTHREAD_COND_INITIALIZER;


//====================================================================================================
//========================= Function: add response to queue ==========================================
void add_response(ResponsePacket add_res) 
{
    pthread_mutex_lock(&resp_mutex);

    response_queue[resp_rear] = add_res;
    resp_rear = (resp_rear + 1) % MAX_QUEUE;

    pthread_cond_signal(&resp_cond);

    pthread_mutex_unlock(&resp_mutex);
}


//====================================================================================================
//========================= Function: take response from queue ========================================
ResponsePacket take_response() 
{
    pthread_mutex_lock(&resp_mutex);

    while (resp_front == resp_rear)
    {
        pthread_cond_wait(&resp_cond, &resp_mutex);
    }
    ResponsePacket take_res = response_queue[resp_front];
    resp_front = (resp_front + 1) % MAX_QUEUE;

    pthread_mutex_unlock(&resp_mutex);
    return take_res;
}

//====================================================================================================
//======================== Thread 1: receive packet from TCP Server ==================================
void *receive_request_thread(void *arg) 
{
    redisContext *redis = redisConnect("127.0.0.1", 6379);
    redisReply   *reply = redisCommand(redis, "SUBSCRIBE modbus_request");

    if (redis == NULL || redis->err) 
    {
        fprintf(stderr, "[RTU Server connect Redis] Connection error: %s\n", redis->errstr);
        return NULL;
    }
    else 
    {
        printf("\n");
        printf("[RTU Server connect Redis] Connected to Redis server\n");
    }
    freeReplyObject(reply);
    printf("[RTU Server connect Redis] Subscribed to modbus_request\n");

    while (1) 
    {
        //-------------------------------------------------------------------------------------------------
        //                     JSON data format:
        //                        "message"                    -> element[0] - type of message,
        //                    "modbus_response"                -> element[1] - channel name,
        // "{\"transaction_id\":1,\"status\":0,\"value\":123}" -> element[2] - main 
        //-------------------------------------------------------------------------------------------------
        redisReply *msg;
        if (redisGetReply(redis, (void **)&msg) == REDIS_OK && msg) 
        {
            if (msg->type == REDIS_REPLY_ARRAY && msg->elements == 3) 
            {
                const char *json_str = msg->element[2]->str;
                json_error_t error;
                json_t *root = json_loads(json_str, 0, &error);
                if (!root) 
                {
                    fprintf(stderr, "[RTU Server receive request] JSON parse error: %s\n", error.text);
                    freeReplyObject(msg);
                    continue;
                }
                RequestPacket req;

                req.transaction_id = json_integer_value(json_object_get(root, "transaction_id"));
                req.rtu_id         = json_integer_value(json_object_get(root, "rtu_id"));
                req.address        = json_integer_value(json_object_get(root, "rtu_address"));
                req.function       = json_integer_value(json_object_get(root, "function"));
                req.quantity       = json_integer_value(json_object_get(root, "quantity"));
                add_request(req);
                json_decref(root); // clean up JSON object

                printf("[RTU Server receive request] Received transaction_id %d, added to queue\n", req.transaction_id);
            }
            freeReplyObject(msg);
        }
    }
    redisFree(redis);

    return NULL;
}

//====================================================================================================
//========================= Thread 2: send command for SmartLogger ===================================
    void *send_command_thread(void *arg) 
{
    modbus_t *ctx = NULL;
    int connected = 0;

    while (1) 
    {
        if (!connected) 
        {
            if (ctx != NULL) 
            {
                modbus_close(ctx);
                modbus_free(ctx);
            }
            ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, PARITY, DATA_BITS, STOP_BITS); 
            if (!ctx) 
            {
                fprintf(stderr, "[RTU Server] Failed to create Modbus RTU context !!!\n");
                sleep(2);
                continue;
            }

            if (modbus_connect(ctx) == -1) 
            {
                fprintf(stderr, "[RTU Server] Modbus RTU connection failed: %s !!!\n", modbus_strerror(errno));
                modbus_free(ctx);
                ctx = NULL;
                sleep(2);
                continue;
            }

            connected = 1;
            printf("[RTU Server] Connected to Modbus RTU device.\n");
        }

        RequestPacket req = take_request();

        // Gán địa chỉ thiết bị
        modbus_set_slave(ctx, req.rtu_id);

        int rc = -1;
        uint16_t value[req.quantity];

        if (req.function == 3) 
        {
            rc = modbus_read_registers(ctx, req.address, req.quantity, value);
            printf("[RTU] Read result: %d\n", rc);
            
        } 
        else if (req.function == 4) 
        {
            rc = modbus_read_input_registers(ctx, req.address, req.quantity, value);
            printf("[RTU] Read result: %d\n", rc);
            
        } 
        else 
        {
            printf("[RTU Server] Unsupported function: %d !!!\n", req.function);
            rc = -1;
        }

        ResponsePacket resp;
        resp.transaction_id = req.transaction_id;

        if (rc != -1) 
        {
            resp.status = 0;
            resp.value = value[0];
            printf("[RTU Server] Transaction_id %d success, value %d .\n", resp.transaction_id, resp.value);
        } 
        else 
        {
            resp.status = 1;
            resp.value = 0;
            connected = 0; 
            printf("[RTU Server] Transaction_id %d failed to get data from device, try again !!!\n", req.transaction_id);
        }

        add_response(resp);
    }

    if (ctx) 
    {
        modbus_close(ctx);
        modbus_free(ctx);
    }

    return NULL;
}


//======================== Thread 3: Gửi phản hồi lên Redis (modbus_response) =====================
void *send_response_thread(void *arg) 
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
        printf("[RTU Server send response] Sent transaction_id %d with status %d and value %d\n", 
            resp.transaction_id, 
            resp.status, 
            resp.value);

        free(json_str);
        json_decref(root);
    }
    
    redisFree(redis);
    return NULL;
}


//====================================================================================================
//======================== Main: create threads and run ==============================================
int main() 
{
    pthread_t recv_t, send_t, resp_t;

    pthread_create(&recv_t, NULL, receive_request_thread, NULL);
    pthread_create(&send_t, NULL, send_command_thread, NULL);
    pthread_create(&resp_t, NULL, send_response_thread, NULL);

    pthread_join(recv_t, NULL);
    pthread_join(send_t, NULL);
    pthread_join(resp_t, NULL);

    return 0;
}

