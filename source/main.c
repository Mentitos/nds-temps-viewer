#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

// Configura estos valores con tu red y servidor
#define SERVER_IP "192.168.1.35"  // IP de tu PC (la que muestra el servidor)
#define SERVER_PORT 5000
#define REFRESH_INTERVAL 60  // Actualización cada 60 frames (~1 segundo)

volatile int frame = 0;

// Timer interrupt para contar frames
void vblankHandler() {
    frame++;
}

// Función para conectar WiFi usando datos guardados en la NDS
void connectWifi() {
    struct in_addr ip, gateway, mask, dns1, dns2;
    
    consoleClear();
    iprintf("Conectando WiFi...\n");
    
    Wifi_InitDefault(INIT_ONLY);
    Wifi_AutoConnect();
    
    while(Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        swiWaitForVBlank();
    }
    
    iprintf("WiFi conectado!\n");
    ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
    iprintf("IP: %s\n", inet_ntoa(ip));
    swiDelay(2000000); // Espera 2 segundos
}

// Hace petición HTTP GET simple
int httpGet(const char* host, int port, const char* path, char* buffer, int bufferSize, int showDebug) {
    struct sockaddr_in serv_addr;
    int sockfd;
    char request[512];
    int bytes_received = 0;
    struct timeval timeout;
    
    if (showDebug) iprintf("Conectando a servidor...\n");
    
    // Crear socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        if (showDebug) iprintf("Error: socket failed\n");
        return -1;
    }
    
    // Configurar timeout (5 segundos)
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Configurar dirección del servidor directamente
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    // Convertir IP string a binario
    if (inet_aton(host, &serv_addr.sin_addr) == 0) {
        if (showDebug) iprintf("Error: IP invalida\n");
        close(sockfd);
        return -2;
    }
    
    // Conectar
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        if (showDebug) iprintf("Error: No se pudo conectar\n");
        close(sockfd);
        return -3;
    }
    
    if (showDebug) iprintf("Enviando peticion...\n");
    
    // Crear petición HTTP GET
    sprintf(request, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", 
        path, host);
    
    // Enviar petición
    if (send(sockfd, request, strlen(request), 0) < 0) {
        if (showDebug) iprintf("Error: send failed\n");
        close(sockfd);
        return -4;
    }
    
    if (showDebug) iprintf("Recibiendo datos...\n");
    
    // Recibir respuesta
    memset(buffer, 0, bufferSize);
    bytes_received = recv(sockfd, buffer, bufferSize - 1, 0);
    
    if (bytes_received <= 0) {
        if (showDebug) iprintf("Error: recv failed\n");
        close(sockfd);
        return -5;
    }
    
    if (showDebug) iprintf("OK! %d bytes recibidos\n", bytes_received);
    
    close(sockfd);
    
    return bytes_received;
}

// Extrae el body del HTTP response
char* extractHttpBody(char* response) {
    // Intenta primero con \r\n\r\n (estándar HTTP)
    char* body = strstr(response, "\r\n\r\n");
    if (body) {
        return body + 4; // Salta los \r\n\r\n
    }
    
    // Si no encuentra, intenta con \n\n (Flask/Werkzeug)
    body = strstr(response, "\n\n");
    if (body) {
        return body + 2; // Salta los \n\n
    }
    
    return NULL;
}

void displayTemperatures(char* data) {
    consoleClear();
    
    // Header
    iprintf("\x1b[2J"); // Clear screen
    iprintf("\x1b[0;0H"); // Move cursor to 0,0
    iprintf("\x1b[47;30m"); // Fondo blanco, texto negro
    iprintf("    MONITOR DE TEMPERATURAS    ");
    iprintf("\x1b[0m\n"); // Reset color
    iprintf("================================\n");
    
    if (data == NULL || strlen(data) == 0) {
        iprintf("\x1b[31mError: Sin datos\x1b[0m\n");
        return;
    }
    
    // Parsear y mostrar cada línea
    char* line = strtok(data, "\n");
    int lineCount = 0;
    
    while (line != NULL && lineCount < 20) {
        // Buscar el valor de temperatura
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0'; // Separar nombre de valor
            char* tempStr = colon + 1;
            
            // Extraer valor numérico
            float temp = 0;
            sscanf(tempStr, " %fC", &temp);
            
            // Colorear según temperatura
            if (temp > 80) {
                iprintf("\x1b[31m"); // Rojo
            } else if (temp > 60) {
                iprintf("\x1b[33m"); // Amarillo
            } else {
                iprintf("\x1b[32m"); // Verde
            }
            
            // Truncar nombre si es muy largo
            char name[22];
            strncpy(name, line, 21);
            name[21] = '\0';
            
            iprintf("%-21s %5.1fC\n", name, temp);
            iprintf("\x1b[0m"); // Reset color
        }
        
        line = strtok(NULL, "\n");
        lineCount++;
    }
    
    iprintf("--------------------------------\n");
    iprintf("\x1b[36mPresiona START para salir\x1b[0m\n");
    iprintf("\x1b[36mPresiona A para actualizar\x1b[0m\n");
}

int main(void) {
    char response[4096];
    char lastData[4096] = "";
    int lastFrame = 0;
    int firstConnection = 1;  // Para mostrar debug solo la primera vez
    
    // Inicializar pantalla superior para texto
    videoSetMode(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    
    consoleDemoInit();
    
    // Configurar interrupt para contar frames
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);
    
    // Conectar WiFi
    connectWifi();
    
    // Pantalla de bienvenida
    consoleClear();
    iprintf("Monitor de Temperaturas NDS\n");
    iprintf("Servidor: %s:%d\n\n", SERVER_IP, SERVER_PORT);
    iprintf("Presiona A para comenzar...\n");
    
    while(1) {
        scanKeys();
        int keys = keysDown();
        
        if (keys & KEY_START) {
            break; // Salir
        }
        
        if (keys & KEY_A) {
            break; // Comenzar
        }
        
        swiWaitForVBlank();
    }
    
    // Loop principal
    while(1) {
        scanKeys();
        int keys = keysDown();
        
        if (keys & KEY_START) {
            break; // Salir
        }
        
        // Actualizar temperaturas automáticamente o con botón A
        if ((frame - lastFrame) >= REFRESH_INTERVAL || (keys & KEY_A)) {
            lastFrame = frame;
            
            consoleClear();
            
            // Mostrar header
            iprintf("\x1b[47;30m");
            iprintf("    MONITOR DE TEMPERATURAS    ");
            iprintf("\x1b[0m\n");
            iprintf("================================\n");
            
            if (firstConnection) {
                iprintf("\x1b[33mConectando por primera vez...\x1b[0m\n\n");
            } else {
                iprintf("\x1b[33mActualizando...\x1b[0m\n");
            }
            
            // Hacer petición HTTP
            int result = httpGet(SERVER_IP, SERVER_PORT, "/api/temps/simple", response, sizeof(response), firstConnection);
            
            if (firstConnection && result > 0) {
                firstConnection = 0;
                swiDelay(2000000); // Pausa 2 segundos para ver que funciono
            }
            
            consoleClear();
            
            if (result > 0) {
                // Extraer body de la respuesta HTTP
                char* body = extractHttpBody(response);
                if (body) {
                    strncpy(lastData, body, sizeof(lastData) - 1);
                    displayTemperatures(lastData);
                } else {
                    iprintf("\x1b[31mError: No se encontro body HTTP\x1b[0m\n\n");
                    iprintf("Respuesta recibida:\n");
                    for(int i = 0; i < 300 && i < result; i++) {
                        iprintf("%c", response[i]);
                    }
                    iprintf("\n\nPresiona A para reintentar\n");
                }
            } else {
                iprintf("\x1b[31mError de conexion: %d\x1b[0m\n\n", result);
                
                switch(result) {
                    case -1:
                        iprintf("No se pudo crear socket\n");
                        break;
                    case -2:
                        iprintf("IP invalida\n");
                        break;
                    case -3:
                        iprintf("No se pudo conectar\n");
                        iprintf("Verifica:\n");
                        iprintf("- Servidor corriendo\n");
                        iprintf("- IP correcta: %s\n", SERVER_IP);
                        iprintf("- Firewall del PC\n");
                        break;
                    case -4:
                        iprintf("Error enviando datos\n");
                        break;
                    case -5:
                        iprintf("Error recibiendo datos\n");
                        iprintf("Timeout o conexion cerrada\n");
                        break;
                }
                
                iprintf("\n\nPresiona A para reintentar\n");
                iprintf("Presiona START para salir\n");
                firstConnection = 1; // Volver a mostrar debug
            }
        }
        
        swiWaitForVBlank();
    }
    
    consoleClear();
    iprintf("Cerrando...\n");
    
    return 0;
}