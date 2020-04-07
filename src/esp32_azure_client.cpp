#include "esp32_azure_client.h"

ESP32AzureClient::ESP32AzureClient()
{
}

ESP32AzureClient::~ESP32AzureClient()
{
    end();
}

bool ESP32AzureClient::begin(const char *connectionString)
{
    if (platform_init() != 0)
    {
        return false;
    }

    if ((client_handle_ = IoTHubDeviceClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol)) == NULL)
    {
        end();
        return false;
    }

    event_queue_ = xQueueCreate(5, sizeof(esp32_azure_event_t));
    reported_state_queue_ = xQueueCreate(5, sizeof(esp32_azure_reported_state_t));

    Callback<void(IOTHUB_CLIENT_CONNECTION_STATUS, IOTHUB_CLIENT_CONNECTION_STATUS_REASON, void *)>::func = std::bind(&ESP32AzureClient::connection_status_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    IOTHUB_CLIENT_CONNECTION_STATUS_CALLBACK func = static_cast<IOTHUB_CLIENT_CONNECTION_STATUS_CALLBACK>(Callback<void(IOTHUB_CLIENT_CONNECTION_STATUS, IOTHUB_CLIENT_CONNECTION_STATUS_REASON, void *)>::callback);
    IoTHubDeviceClient_LL_SetConnectionStatusCallback(client_handle_, func, NULL);

    Callback<void(DEVICE_TWIN_UPDATE_STATE, const unsigned char *, size_t, void *)>::func = std::bind(&ESP32AzureClient::device_twin_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK device_twin_cb = static_cast<IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK>(Callback<void(DEVICE_TWIN_UPDATE_STATE, const unsigned char *, size_t, void *)>::callback);
    // TODO: need to find a way to embed old device twin to the context. Or, is it necessary?
    IoTHubDeviceClient_LL_SetDeviceTwinCallback(client_handle_, device_twin_cb, NULL);

    Callback<void(int, void *)>::func = std::bind(&ESP32AzureClient::reported_state_callback, this, std::placeholders::_1, std::placeholders::_2);
    reported_state_cb_ = static_cast<IOTHUB_CLIENT_REPORTED_STATE_CALLBACK>(Callback<void(int, void *)>::callback);

    Callback<void(IOTHUB_CLIENT_CONFIRMATION_RESULT, void *)>::func = std::bind(&ESP32AzureClient::send_confirmation_callback, this, std::placeholders::_1, std::placeholders::_2);
    send_confirmation_cb_ = static_cast<IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK>(Callback<void(IOTHUB_CLIENT_CONFIRMATION_RESULT, void *)>::callback);

    // TODO: setup callback for direct method invocation

    xTaskCreate(&runTask, "esp32_azure_task", 1024 * 8, this, 10, NULL);

    return true;
}

void ESP32AzureClient::end()
{
    if (client_handle_ != NULL)
    {
        IoTHubDeviceClient_LL_Destroy(client_handle_);
    }

    platform_deinit();
}

void ESP32AzureClient::run()
{
    while (true)
    {
        esp32_azure_event_t event;
        if (xQueueReceive(event_queue_, &event, 50 / portTICK_PERIOD_MS) == pdPASS)
        {
            // Received pending event from event_queue_
            if (IoTHubDeviceClient_LL_SendEventAsync(client_handle_, event.handle, send_confirmation_cb_, &event) != IOTHUB_CLIENT_OK)
            {
                ESP32_AZURE_LOGE("ERROR: IoTHubDeviceClient_LL_SendEventAsync..........FAILED!");
            }
            else
            {
                ESP32_AZURE_LOGI("IoTHubDeviceClient_LL_SendEventAsync accepted message [%d] for transmission to IoT Hub.\r\n", event.tracking_id);
            }
        }

        esp32_azure_reported_state_t state;
        if (xQueueReceive(reported_state_queue_, &state, 50 / portTICK_PERIOD_MS) == pdPASS)
        {
            IOTHUB_CLIENT_RESULT result = IoTHubDeviceClient_LL_SendReportedState(client_handle_, (const unsigned char *)state.payload, strlen(state.payload), reported_state_cb_, &state);
            if (result != IOTHUB_CLIENT_OK)
            {
                ESP32_AZURE_LOGE("ERROR: IoTHubDeviceClient_LL_SendReportedState..........FAILED!");
            }
            else
            {
                ESP32_AZURE_LOGI("IoTHubDeviceClient_LL_SendReportedState OK!");
            }
        }

        IoTHubDeviceClient_LL_DoWork(client_handle_);
        ThreadAPI_Sleep(10);
    }
}

void ESP32AzureClient::runTask(void *param)
{
    ESP32AzureClient *self = (ESP32AzureClient *)param;

    self->run();
    vTaskDelete(NULL);
}

bool ESP32AzureClient::sendEvent(const char *payload, size_t length)
{
    static int message_iterator = 0;
    esp32_azure_event_t event;

    if ((event.handle = IoTHubMessage_CreateFromByteArray((const unsigned char *)payload, length)) == NULL)
    {
        return false;
    }

    event.tracking_id = message_iterator++;

    if (xQueueSendToBack(event_queue_, &event, 100 / portTICK_PERIOD_MS) == pdFAIL)
    {
        // Queue is full
        return false;
    }

    // Enqueued
    return true;
}

bool ESP32AzureClient::reportState(const char *payload, size_t length)
{
    esp32_azure_reported_state_t state;
    state.payload = (char *)malloc(sizeof(char*)*length);

    if (state.payload == NULL)
    {
        return false;
    }

    memset(state.payload, 0, length);

    state.length = length;
    strcpy(state.payload, payload);

    if (xQueueSendToBack(reported_state_queue_, &state, 100 / portTICK_PERIOD_MS) == pdFAIL)
    {
        free(state.payload);

        return false;
    }

    return true;
}

void ESP32AzureClient::connection_status_callback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *user_context)
{
    ESP32_AZURE_LOGI("Connection status: %s, reason: %s",
                     ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result),
                     ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
    if (connection_status_user_cb_)
    {
        connection_status_user_cb_(result, reason, user_context);
    }
}

void ESP32AzureClient::device_twin_callback(DEVICE_TWIN_UPDATE_STATE state, const unsigned char *payload, size_t size, void *context)
{
    ESP32_AZURE_LOGI("Received device twin:\r\n<<<%.*s>>>", (int)size, payload);
    
    if (device_twin_user_cb_)
    {
        device_twin_user_cb_(state, payload, size, context);
    }
}

void ESP32AzureClient::reported_state_callback(int status_code, void *context)
{
    ESP32_AZURE_LOGI("Reported state delivery resulted with code: %d", status_code);

    if (reported_state_user_cb_)
    {
        reported_state_user_cb_(status_code, context);
    }

    if (context != NULL)
    {
        esp32_azure_reported_state_t *state = (esp32_azure_reported_state_t *)context;
        free(state->payload);
    }
}

void ESP32AzureClient::send_confirmation_callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    esp32_azure_event_t *event = (esp32_azure_event_t *)context;
    int id = event->tracking_id;

    if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
    {
        ESP32_AZURE_LOGI("Confirmation received for message tracking id = %d with result = %s", (int)id, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    }

    if (message_confirm_user_cb_)
    {
        message_confirm_user_cb_(result);
    }

    IoTHubMessage_Destroy(event->handle);
}

void ESP32AzureClient::onConnectionStatusChanged(ConnectionStatusCallback callback)
{
    connection_status_user_cb_ = callback;
}

void ESP32AzureClient::onEventConfirmed(DeviceMessageConfirmationCallback callback)
{
    message_confirm_user_cb_ = callback;
}

void ESP32AzureClient::onReportedStatedDelivered(ReportedStateCallback callback)
{
    reported_state_user_cb_ = callback;
}

void ESP32AzureClient::onDeviceTwinReceived(DeviceTwinCallback callback)
{
    device_twin_user_cb_ = callback;
}