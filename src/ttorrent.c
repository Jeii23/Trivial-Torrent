
// Trivial Torrent

#include "file_io.h"
#include "logger.h"
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>

/**
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3233; // = htonl(0x33321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;
#define LEN_FILE 10000
#define TIMEOUT 8000

enum {
    RAW_MESSAGE_SIZE = 13
};

void get_name_of_torrent(char* path);
void update_pfds(int i, struct pollfd *pfds, nfds_t *fd_count);

void get_name_of_torrent(char *path)
{
    char *dot_torrent = strrchr(path, '.');
    if (dot_torrent)
    {
        *dot_torrent = '\0'; // Fin de string.
    }
}

void update_pfds(int i, struct pollfd *pfds, nfds_t *fd_count)
{
    close(pfds[i].fd);
    // Actualizar la lista de sockets
    for (int k = i; k < (int) *fd_count - 1; k++)
    {
        pfds[k] = pfds[k + 1];
    }
    (*fd_count)--;
}

/**
 * Main function.
 */
int main(int argc, char **argv)
{
    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "J. DOE and J. DOE");

    // ==========================================================================
    // Parse command line
    // ==========================================================================

    // TODO: some magical lines of code here that call other functions and do various stuff.

    // The following statements most certainly will need to be deleted at some point...
    (void) argc;
    (void) argv;
    (void) MAGIC_NUMBER;
    (void) MSG_REQUEST;
    (void) MSG_RESPONSE_NA;
    (void) MSG_RESPONSE_OK;

    // Creación de estructuras.
    struct torrent_t torrent;
    struct sockaddr_in addr_client;
    struct sockaddr_in addr_server;
    struct pollfd pfds[2] = { 0 };


    if (argc == 2) // Caso cliente.
    {
        // Tratamiento de path para obtener el nombre sin la extension .ttorrent.
        char name_of_torrent[LEN_FILE] = "";
        strcpy(name_of_torrent,argv[1]);
        get_name_of_torrent(name_of_torrent);

        // 1. Cargar el archivo de metadatos.
        if (create_torrent_from_metainfo_file(argv[1], &torrent, name_of_torrent) < 0)
        {
            //Caso de error.
            perror("El Torrent no ha podido ser creado.");
            return -1;
        }

        uint64_t downloaded_count = 0;  // Variable para controlar el número de bloques descargados.
        uint64_t block_number = 0;       // blockNumber del bucle anidado.

        // 2. Por cada peer:
        for (uint64_t i = 0; i < torrent.peer_count; i++)
        {
            // Creación del socket.
            int sckt = socket(AF_INET, SOCK_STREAM, 0);
            if (sckt < 0)
            {
                // Caso de error, avanzamos al siguiente peer.
                perror("El Socket no ha podido ser creado.");
                close(sckt);
                continue;
            }

            uint32_t address_ip_client;
            uint16_t port_of_peer = torrent.peers[i].peer_port;

            // Procedimiento para obtener la dirección.
            address_ip_client = (uint32_t) torrent.peers[i].peer_address[3];
            address_ip_client += (uint32_t) torrent.peers[i].peer_address[2] << 8;
            address_ip_client += (uint32_t) torrent.peers[i].peer_address[1] << 16;
            address_ip_client += (uint32_t) torrent.peers[i].peer_address[0] << 24;

            // Inicialización de las variables de la estructura del cliente.
            addr_client.sin_family = AF_INET;
            addr_client.sin_port = port_of_peer;
            addr_client.sin_addr.s_addr = htonl(address_ip_client);

            // 2.1 Conectamos el socket.
            if (connect(sckt, (const struct sockaddr *) &addr_client, sizeof(addr_client)) < 0)
            {
                // Caso de error, avanzamos al siguiente peer.
                perror("El Socket no ha sido conectado correctamente.");
                close(sckt);
                continue;
            }

            // Por cada bloque.
            for (; block_number < torrent.block_count; block_number++)
            {
                // 2.2 Por cada bloque que tengamos.
                if (torrent.block_map[block_number] != 0)
                {
                    downloaded_count++; // Si ya lo teniamos disponible.
                    continue;
                }

                // Inicialización de la cabecera a enviar.
                uint8_t header_send[RAW_MESSAGE_SIZE];

                uint8_t auxiliar;
                uint32_t magic_num = MAGIC_NUMBER;

                // Tratamiento del Magic Number.
                auxiliar = (uint8_t) (magic_num >> 24); // 4rt per cua.
                header_send[0] = auxiliar;
                auxiliar = (uint8_t) (magic_num >> 16); // 3r per cua.
                header_send[1] = auxiliar;
                auxiliar = (uint8_t) (magic_num >> 8);  // Penúltim octet.
                header_send[2] = auxiliar;
                auxiliar = (uint8_t) magic_num;         // Últim octet.
                header_send[3] = auxiliar;

                // Mensaje.
                header_send[4] = MSG_REQUEST;

                // BlockNumber.
                header_send[5] = (uint8_t) (block_number << 56);
                header_send[6] = (uint8_t) (block_number << 48);
                header_send[7] = (uint8_t) (block_number << 40);
                header_send[8] = (uint8_t) (block_number << 32);
                header_send[9] = (uint8_t) (block_number << 24);
                header_send[10] = (uint8_t) (block_number << 16);
                header_send[11] = (uint8_t) (block_number << 8);
                header_send[12] = (uint8_t) block_number;

                // Enviamos cabecera inicializada.
                if (send(sckt, header_send, RAW_MESSAGE_SIZE, 0) < 0)
                {
                    // Caso de error.
                    perror("La solicitud de envio ha sido rechazada.");
                    close(sckt);
                    break;
                }

                // Creamos otro header para recibir el resultado.
                uint8_t header_received[RAW_MESSAGE_SIZE];

                if (recv(sckt, header_received, RAW_MESSAGE_SIZE, MSG_WAITALL) < 0) // Si el servidor respon amb el bloc.
                {
                    // Caso de error.
                    perror("La solicitud de recibimiento ha sido rechazada 1.");
                    close(sckt);
                    break;
                }

                if (header_received[4] != MSG_RESPONSE_OK)
                {
                    // El servidor no responde con el bloque.
                    continue;
                }

                // El servidor responde con el bloque.
                struct block_t block;
                block.size = get_block_size(&torrent, block_number);

                // Llenamos el bloque.
                if (recv(sckt, block.data, block.size, MSG_WAITALL) < 0) // Si el servidor respon amb el bloc.
                {
                    assert(block.data != NULL);
                    perror("La solicitud de recibimiento ha sido rechazada 2.");
                    close(sckt);
                    break;
                }

                // Guardamos el bloque a disco.
                if (store_block(&torrent, block_number, &block) < 0)
                {
                    // Caso de error.
                    perror("El bloque no ha sido almacenado correctamente.");
                    close(sckt);
                    break;
                }
                else
                {
                    // Aseguramos que el bloque se haya descargado correctamente.
                    assert(torrent.block_map[block_number]);
                    log_printf(LOG_DEBUG, "BlockNumber: %d Descargado desde peer: %d", block_number, i);
                    downloaded_count++; // Como ha sido almacenado correctamente.
                }
            }

            // Comprobamos si hemos finalizado.
            if (downloaded_count == torrent.block_count)
            {
                log_printf(LOG_DEBUG, "Fin del client.");
                close(sckt);
                break;
            }
        }
    }
    else
    {
        if (argc == 4) // Caso server.
        {
            // Tratamiento de path para obtener nombre.
            char name_of_torrent[LEN_FILE] = "";
            strcpy(name_of_torrent, argv[3]);
            get_name_of_torrent(name_of_torrent);

            // 1. Cargar el archivo de metadatos.
            if (create_torrent_from_metainfo_file(argv[3], &torrent, name_of_torrent) < 0)
            {
                //Caso de error.
                perror("El Torrent no ha podido ser creado.");
                return -1;
            }

            // Inicialización de las variables de la estructura del cliente.
            addr_server.sin_family = AF_INET;
            addr_server.sin_port = htons( (uint16_t) atoi(argv[2]));
            addr_server.sin_addr.s_addr = htonl(INADDR_ANY);

            // Creación server socket.
            int server_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (server_socket < 0)
            {
                // Caso error socket.
                perror("El Socket no ha podido ser creado.");
                close(server_socket);
                destroy_torrent(&torrent);
                return -1;
            }

            // Establecer tamaño mínimo para recibir.
            int optval = RAW_MESSAGE_SIZE;
            socklen_t optlen = sizeof(optval);

            if(setsockopt(server_socket, SOL_SOCKET, SO_RCVLOWAT, &optval, optlen) < 0)
            {
                perror("Se ha producido un error al establecer SO_RCVLOWAT.");
                close(server_socket);
                destroy_torrent(&torrent);
                return -1;
            }

            // Establecer propiedad no bloqueadora.
            if (fcntl(server_socket, F_SETFL, fcntl(server_socket, F_GETFL) | O_NONBLOCK) < 0)
            {
                perror("El fcntl no se ha establecido la propiedad nonblock.");
                close(server_socket);
                destroy_torrent(&torrent);
                return -1;
            }

            // Creamos bind.
            if (bind(server_socket, (const struct sockaddr*) &addr_server, sizeof(addr_server)) < 0)
            {
                // Caso Error bind.
                perror("La unión ha sido fallida.");
                close(server_socket);
                destroy_torrent(&torrent);
                return -1;
            }

            // Esperamos conexiones.
            if (listen(server_socket, 2048) < 0)
            {
                // Caso error listen.
                perror("El socket ha fallado al escuchar las connecciones.");
                close(server_socket);
                destroy_torrent(&torrent);
                return -1;
            }

            // Definición contador y tamaño del array.
            nfds_t fd_count = 0;
            nfds_t fd_size = 2;

            // El primer socket lo pondremos a la primera posición de pollfd.
            pfds[0].fd = server_socket;
            pfds[0].events = POLLIN;
            fd_count++;

            uint8_t header_received[RAW_MESSAGE_SIZE];

            while(1)
            {
                // Bucle para llamar a la función poll.
                int segment = poll(pfds, fd_count, TIMEOUT);
                if (segment < 0)
                {
                    perror("ERROR: poll_count");
                    close(server_socket);
                    destroy_torrent(&torrent);
                    return -1;
                }
                // TIMEOUT
                if (segment == 0)
                {
                    log_printf(LOG_DEBUG,"%d Segundos transcurridos. Cliente no responde. \n", TIMEOUT / 1000);
                    continue;
                }

                // Bucle para monitorizar los sockets totales.
                for(int i = 0; i < (int) fd_count; i++)
                {
                    if (pfds[i].revents & POLLIN && pfds[i].fd == server_socket) // Polling & descriptor a listen
                    {
                        socklen_t server_length = sizeof(addr_server);
                        int new_socket = accept(server_socket, (struct sockaddr*) &addr_server, &server_length);
                        if (new_socket < 0)
                        {
                            //Caso error accept.
                            perror("El socket no ha podido sincronizarse");
                            close(new_socket);
                            continue;
                        }

                        if (fd_size <= fd_count)
                        {
                            log_printf(LOG_DEBUG,"El count ha superat el size");
                            close(pfds[i].fd);
                            break;
                        }

                        // Añadir nuevo socket al vector pfds.
                        pfds[i+1].fd = new_socket;
                        pfds[i+1].events = POLLIN;
                        fd_count++;

                        if(setsockopt(new_socket, SOL_SOCKET, SO_RCVLOWAT, &optval, optlen) < 0)
                        {
                            perror("Se ha producido un error al establecer SO_RCVLOWAT.");
                            close(pfds[i+1].fd);
                            destroy_torrent(&torrent);
                            return -1;
                        }

                        if (fcntl(pfds[i+1].fd, F_SETFL, fcntl(pfds[i+1].fd, F_GETFL) | O_NONBLOCK) < 0) // Establecer propiedad no bloqueadora.
                        {
                            perror("La función fcntl no se ha establecido la propiedad nonblock.");
                            close(pfds[i+1].fd);
                            destroy_torrent(&torrent);
                            return -1;
                        }
                        continue;
                    }

                    //Socket no listen

                    // Caso POLLIN
                    if (pfds[i].revents & POLLIN)
                    {
                        log_printf(LOG_DEBUG,"Monitorizar POLLIN.");

                        ssize_t value_received = recv(pfds[i].fd, header_received, RAW_MESSAGE_SIZE, 0);
                        if (value_received <= 0)
                        {
                            if (value_received == -1) // Si el servidor no responde con el bloque.
                            {
                                perror("La solicitud de recibimiento ha sido rechazada.");
                            }
                            else
                            {
                                perror("El cliente ha cerrado la conexion.");
                            }

                            update_pfds(i, pfds, &fd_count);
                            continue;
                        }

                        // Tratamiento de magic number.
                        uint32_t magic_num;
                        magic_num = (uint32_t) header_received[3];
                        magic_num += (uint32_t) header_received[2] << 8;
                        magic_num += (uint32_t) header_received[1] << 16;
                        magic_num += (uint32_t) header_received[0] << 24;

                        if (magic_num != MAGIC_NUMBER) // Comprobación Magic number incorrecto.
                        {
                            log_printf(LOG_DEBUG,"Magic Number incorrecto");
                            close(pfds[i].fd);
                            break;
                        }

                        // Comprobación REQUEST.
                        if (header_received[4] != MSG_REQUEST)
                        {
                            log_printf(LOG_DEBUG,"MSG REQUEST no recibido.");
                            close(pfds[i].fd);
                            break;
                        }

                        // Volvemos a enviar datos para el socket.
                        pfds[i].events = POLLOUT;

                        log_printf(LOG_DEBUG,"Datos Recibidos");
                    }

                    // Caso POLLLER.
                    if (pfds[i].revents & POLLERR)
                    {
                        // Tratamiento de error.
                        log_printf(LOG_DEBUG,"Tratamiento de error POLLERR.");
                        update_pfds(i, pfds, &fd_count);
                        break;
                    }

                    // Caso POLLOUT.
                    if (pfds[i].revents & POLLOUT) // PODEM ENVIAR LES DADES DEL BUFFER.
                    {
                        log_printf(LOG_DEBUG,"Monitorizar POLLOUT.");

                        // Tratamiento del block_number.
                        uint64_t block_number;
                        block_number = (uint64_t) header_received[12];
                        block_number += (uint64_t) header_received[11] << 8;
                        block_number += (uint64_t) header_received[10] << 16;
                        block_number += (uint64_t) header_received[9] << 24;
                        block_number += (uint64_t) header_received[8] << 32;
                        block_number += (uint64_t) header_received[7] << 40;
                        block_number += (uint64_t) header_received[6] << 48;
                        block_number += (uint64_t) header_received[5] << 56;

                        struct block_t block;
                        block.size = get_block_size(&torrent, block_number);
                        if (block.size == 0)
                        {
                            // Caso error.
                            perror("El bloque es inexistente");
                            close(pfds[i].fd);
                            break;
                        }

                        if (torrent.block_map[block_number] != 1) // No tenemos el bloque disponible.
                        {

                            header_received[4] = MSG_RESPONSE_NA;
                            if (send(pfds[i].fd,header_received,RAW_MESSAGE_SIZE,0) < 0)
                            {
                                // Caso de error.
                                perror("El bloque no ha podido ser enviado.");
                                close(pfds[i].fd);
                                break;
                            }
                            continue;
                        }

                        // Tenemos el bloque disponible.
                        if (load_block(&torrent, block_number, &block) < 0)
                        {
                            perror("El bloque no ha podido ser cargado.");
                            close(pfds[i].fd);
                            break;
                        }

                        header_received[4] = MSG_RESPONSE_OK;

                        // Envio de cabecera.
                        if (send(pfds[i].fd, header_received, RAW_MESSAGE_SIZE, 0) < 0)
                        {
                            // Caso de error.
                            perror("La solicitud de envio del header ha sido rechazada.");
                            close(pfds[i].fd);
                            break;
                        }

                        // Envio de bloque.
                        if (send(pfds[i].fd, block.data, block.size, 0) < 0)
                        {
                            // Caso de error.
                            perror("La solicitud de envio del bloque ha sido rechazada.");
                            close(pfds[i].fd);
                            break;
                        }

                        // Volvemos a recibir datos.
                        pfds[i].events = POLLIN;

                        log_printf(LOG_DEBUG,"Datos enviados.");
                    }
                }
            }
        }
    }

    log_printf(LOG_DEBUG, "TERMINADO.");

    // Programa ejecutado correctamente.
    return 0;
}
