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
#define SERVER_IP "192.168.1.35"
#define SERVER_PORT 5000
#define REFRESH_INTERVAL 60

volatile int frame = 0;

void vblankHandler() {
    frame++;
}

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
    swiDelay(2000000);
}

int httpGet(const char* host, int port, const char* path, char* buffer, int bufferSize) {
    struct sockaddr_in serv_addr;
    int sockfd;
    char request[512];
    int bytes_received = 0;
    struct timeval timeout;
    
    iprintf("1.Socket...\n");
    swiWaitForVBlank();
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        iprintf("ERROR socket\n");
        return -1;
    }
    
    iprintf("2.Timeout...\n");
    swiWaitForVBlank();
    
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    iprintf("3.Config addr...\n");
    swiWaitForVBlank();
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_aton(host, &serv_addr.sin_addr) == 0) {
        iprintf("ERROR IP\n");
        close(sockfd);
        return -2;
    }
    
    iprintf("4.Connect...\n");
    swiWaitForVBlank();
    
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        iprintf("ERROR connect\n");
        close(sockfd);
        return -3;
    }
    
    iprintf("5.Send...\n");
    swiWaitForVBlank();
    
    sprintf(request, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", 
        path, host);
    
    if (send(sockfd, request, strlen(request), 0) < 0) {
        iprintf("ERROR send\n");
        close(sockfd);
        return -4;
    }
    
    iprintf("6.Recv...\n");
    swiWaitForVBlank();
    
    memset(buffer, 0, bufferSize);
    bytes_received = recv(sockfd, buffer, bufferSize - 1, 0);
    
    iprintf("7.Close... (%d bytes)\n", bytes_received);
    swiWaitForVBlank();
    
    close(sockfd);
    
    if (bytes_received <= 0) {
        iprintf("ERROR recv\n");
        return -5;
    }
    
    iprintf("8.OK!\n");
    swiWaitForVBlank();
    
    return bytes_received;
}

char* extractHttpBody(char* response) {
    char* body = strstr(response, "\r\n\r\n");
    if (body) return body + 4;
    
    body = strstr(response, "\n\n");
    if (body) return body + 2;
    
    return NULL;
}

void displayTemperatures(char* data) {
    consoleClear();
    
    iprintf("================================\n");
    iprintf("   TEMPERATURAS DEL PC\n");
    iprintf("================================\n\n");
    
    if (data == NULL || strlen(data) == 0) {
        iprintf("Error: Sin datos\n");
        return;
    }
    
    char* line = strtok(data, "\n");
    int lineCount = 0;
    
    while (line != NULL && lineCount < 18) {
        while (*line == ' ') line++;
        
        if (strlen(line) > 0) {
            float temp = 0;
            char* tempPos = strchr(line, ':');
            
            if (tempPos) {
                sscanf(tempPos + 1, " %fC", &temp);
                
                if (temp > 80) {
                    iprintf("\x1b[31m");
                } else if (temp > 60) {
                    iprintf("\x1b[33m");
                } else if (temp > 0) {
                    iprintf("\x1b[32m");
                }
            }
            
            char display[32];
            strncpy(display, line, 31);
            display[31] = '\0';
            iprintf("%s\n", display);
            
            if (tempPos) {
                iprintf("\x1b[0m");
            }
            
            lineCount++;
        }
        
        line = strtok(NULL, "\n");
    }
    
    iprintf("\n--------------------------------\n");
    iprintf("Actualiza cada ~1 segundo\n");
    iprintf("START = Salir\n");
}

int main(void) {
    char response[4096];
    char lastData[4096] = "";
    int lastFrame = 0;
    
    videoSetMode(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    consoleDemoInit();
    
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);
    
    connectWifi();
    
    consoleClear();
    iprintf("Monitor de Temperaturas NDS\n");
    iprintf("Servidor: %s:%d\n\n", SERVER_IP, SERVER_PORT);
    iprintf("Presiona A para comenzar...\n");
    
    while(1) {
        scanKeys();
        if (keysDown() & (KEY_START | KEY_A)) break;
        swiWaitForVBlank();
    }
    
    // Loop principal
    while(1) {
        scanKeys();
        int keys = keysDown();
        
        if (keys & KEY_START) break;
        
        if ((frame - lastFrame) >= REFRESH_INTERVAL || (keys & KEY_A)) {
            lastFrame = frame;
            
            consoleClear();
            iprintf("================================\n");
            iprintf("   MONITOR DE TEMPERATURAS\n");
            iprintf("================================\n");
            iprintf("Actualizando...\n\n");
            
            int result = httpGet(SERVER_IP, SERVER_PORT, "/api/temps/simple", response, sizeof(response));
            
            iprintf("HTTP result: %d\n", result);
            swiDelay(1000000); // Pausa 1 seg para ver el resultado
            
            if (result > 0) {
                iprintf("Extrayendo body...\n");
                char* body = extractHttpBody(response);
                
                if (body != NULL && strlen(body) > 5) {
                    strncpy(lastData, body, sizeof(lastData) - 1);
                    displayTemperatures(lastData);
                } else {
                    consoleClear();
                    iprintf("Error: No se pudo extraer datos\n\n");
                    
                    if (body == NULL) {
                        iprintf("Body HTTP no encontrado\n\n");
                        iprintf("Respuesta (primeros 300 chars):\n");
                        for(int i = 0; i < 300 && i < result; i++) {
                            iprintf("%c", response[i]);
                        }
                    } else {
                        iprintf("Body muy corto: %d chars\n", strlen(body));
                    }
                    
                    iprintf("\n\nPresiona A para reintentar");
                }
            } else {
                consoleClear();
                iprintf("Error de conexion: %d\n\n", result);
                
                if (result == -3) {
                    iprintf("No se pudo conectar\n");
                    iprintf("Verifica firewall y servidor\n");
                } else if (result == -5) {
                    iprintf("Timeout recibiendo datos\n");
                }
                
                iprintf("\n\nPresiona A para reintentar");
            }
        }
        
        swiWaitForVBlank();
    }
    
    consoleClear();
    iprintf("Cerrando...\n");
    
    return 0;
}