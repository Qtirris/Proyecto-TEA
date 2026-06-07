El archivo se encuentra Conectividad.ino esta comentado. Ante dudas escribale a Stick o revisen la documentación.

Aca estan cosas a tener en cuenta.

1. WiFi (https://docs.arduino.cc/libraries/wifi/#Wifi%20Class)
    ESTADOS DE LA ESP EN MODO STATION:
        0	WL_IDLE_STATUS	temporary status assigned when WiFi.begin() is called
        1	WL_NO_SSID_AVAIL	 when no SSID are available
        2	WL_SCAN_COMPLETED	scan networks is completed
        3	WL_CONNECTED	when connected to a WiFi network
        4	WL_CONNECT_FAILED	when the connection fails for all the attempts
        5	WL_CONNECTION_LOST	when the connection is lost
        6	WL_DISCONNECTED	when disconnected from a network

    Revisar eventos de WiFi
        0	ARDUINO_EVENT_WIFI_READY	ESP32 Wi-Fi ready
        1	ARDUINO_EVENT_WIFI_SCAN_DONE	ESP32 finishes scanning AP
        2	ARDUINO_EVENT_WIFI_STA_START	ESP32 station start
        3	ARDUINO_EVENT_WIFI_STA_STOP	ESP32 station stop
        4	ARDUINO_EVENT_WIFI_STA_CONNECTED	ESP32 station connected to AP
        5	ARDUINO_EVENT_WIFI_STA_DISCONNECTED	ESP32 station disconnected from AP
        6	ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE	the auth mode of AP connected by ESP32 station changed
        7	ARDUINO_EVENT_WIFI_STA_GOT_IP	ESP32 station got IP from connected AP
        8	ARDUINO_EVENT_WIFI_STA_LOST_IP	ESP32 station lost IP and the IP is reset to 0
        9	ARDUINO_EVENT_WPS_ER_SUCCESS	ESP32 station wps succeeds in enrollee mode
        10	ARDUINO_EVENT_WPS_ER_FAILED	ESP32 station wps fails in enrollee mode
        11	ARDUINO_EVENT_WPS_ER_TIMEOUT	ESP32 station wps timeout in enrollee mode
        12	ARDUINO_EVENT_WPS_ER_PIN	ESP32 station wps pin code in enrollee mode
        13	ARDUINO_EVENT_WIFI_AP_START	ESP32 soft-AP start
        14	ARDUINO_EVENT_WIFI_AP_STOP	ESP32 soft-AP stop
        15	ARDUINO_EVENT_WIFI_AP_STACONNECTED	a station connected to ESP32 soft-AP
        16	ARDUINO_EVENT_WIFI_AP_STADISCONNECTED	a station disconnected from ESP32 soft-AP
        17	ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED	ESP32 soft-AP assign an IP to a connected station
        18	ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED	Receive probe request packet in soft-AP interface
        19	ARDUINO_EVENT_WIFI_AP_GOT_IP6	ESP32 access point v6IP addr is preferred
        19	ARDUINO_EVENT_WIFI_STA_GOT_IP6	ESP32 station v6IP addr is preferred
        19	ARDUINO_EVENT_ETH_GOT_IP6	Ethernet IPv6 is preferred
        20	ARDUINO_EVENT_ETH_START	ESP32 ethernet start
        21	ARDUINO_EVENT_ETH_STOP	ESP32 ethernet stop
        22	ARDUINO_EVENT_ETH_CONNECTED	ESP32 ethernet phy link up
        23	ARDUINO_EVENT_ETH_DISCONNECTED	ESP32 ethernet phy link down
        24	ARDUINO_EVENT_ETH_GOT_IP	ESP32 ethernet got IP from connected AP
        25	ARDUINO_EVENT_MAX	

    Revisar WiFiMulti

2. HttpClient
    /// HTTP client errors
    #define HTTPC_ERROR_CONNECTION_REFUSED  (-1)
    #define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
    #define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
    #define HTTPC_ERROR_NOT_CONNECTED       (-4)
    #define HTTPC_ERROR_CONNECTION_LOST     (-5)
    #define HTTPC_ERROR_NO_STREAM           (-6)
    #define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
    #define HTTPC_ERROR_TOO_LESS_RAM        (-8)
    #define HTTPC_ERROR_ENCODING            (-9)
    #define HTTPC_ERROR_STREAM_WRITE        (-10)
    #define HTTPC_ERROR_READ_TIMEOUT        (-11)

    Todos los metodos y constantes de la libreria estan definidos en el github de la misma. (https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPClient/src/HTTPClient.h)

    Revisar como imprimir solo una parte del encanbezado de respuesta.
    Revisar como(form, data, multipart) y que info se necesita enviar.

3. HTTP 
    Peticiones:
        POST:
        GET:
        PUT:
        PATH:
        DELETE:
        HEAD:
        OPTIONS:
        TRACE:

    Codigos:
        1xx:    Info
        2xx:    Exito
        3xx:    Redireccionamiento
        4xx:    Error del cliente
        5xx:    Error del servidor

    RESIVAR LOS CODIGOS PARA LA LINEA 78 CONEVTIVIDAD.INO

Probar push de Valetor :
