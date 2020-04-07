#ifndef DYCODEX_ESP32_AZURE_H_
#define DYCODEX_ESP32_AZURE_H_

#include <functional>
#include "esp_log.h"
#include "parson.h"
#include "iothub_client.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_options.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp32_azure_client_callback.h"

#define ESP32_AZURE_LOGI(...) ESP_LOGI("ESP32 Azure", __VA_ARGS__)
#define ESP32_AZURE_LOGE(...) ESP_LOGE("ESP32 Azure", __VA_ARGS__)

class ESP32AzureClient
{
public:
    typedef struct esp32_azure_event_tag
    {
        IOTHUB_MESSAGE_HANDLE handle;
        int tracking_id;
    } esp32_azure_event_t;

    typedef struct esp32_azure_reported_state_tag
    {
        char* payload;
        size_t length;
    } esp32_azure_reported_state_t;

    typedef std::function<void(IOTHUB_CLIENT_CONNECTION_STATUS, IOTHUB_CLIENT_CONNECTION_STATUS_REASON, void *)> ConnectionStatusCallback;
    typedef std::function<void(DEVICE_TWIN_UPDATE_STATE, const unsigned char *, size_t, void *)> DeviceTwinCallback;
    typedef std::function<void(int, void *)> ReportedStateCallback;
    typedef std::function<void(IOTHUB_CLIENT_CONFIRMATION_RESULT result)> DeviceMessageConfirmationCallback;

    ESP32AzureClient();
    ~ESP32AzureClient();

    bool begin(const char *connectionString);
    void end();
    bool sendEvent(const char *payload, size_t length);
    bool reportState(const char *payload, size_t length);

    void onConnectionStatusChanged(ConnectionStatusCallback callback);
    void onEventConfirmed(DeviceMessageConfirmationCallback callback);
    void onReportedStatedDelivered(ReportedStateCallback callback);
    void onDeviceTwinReceived(DeviceTwinCallback callback);

private:
    static void runTask(void *context);

    // called from runTask
    void run();

    // internal callbacks
    void connection_status_callback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *user_context);
    void device_twin_callback(DEVICE_TWIN_UPDATE_STATE state, const unsigned char *payload, size_t size, void *context);
    void reported_state_callback(int status_code, void *context);
    void send_confirmation_callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context);

private:
    IOTHUB_DEVICE_CLIENT_LL_HANDLE client_handle_ = NULL;

    // This won't be assigned to the IoTHubDevice LL Client until the reported state is sent to Azure IoT Hub
    IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reported_state_cb_;

    IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK send_confirmation_cb_;

    // user callbacks
    ConnectionStatusCallback connection_status_user_cb_;

    DeviceMessageConfirmationCallback message_confirm_user_cb_;

    ReportedStateCallback reported_state_user_cb_;

    DeviceTwinCallback device_twin_user_cb_;

    QueueHandle_t event_queue_;
    QueueHandle_t reported_state_queue_;
    QueueHandle_t device_twin_queue_;
    QueueHandle_t cloud_message_queue_;
};

#endif // DYCODEX_ESP32_AZURE_H_