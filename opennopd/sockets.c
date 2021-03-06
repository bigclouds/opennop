#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h> // for multi-threading
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>

#include <linux/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ipc.h"
#include "logger.h"
#include "sockets.h"

#define MAXEVENTS 64

static int DEBUG_SOCKETS = LOGGING_OFF;

int new_udp_client(__u32 serverip ,int port) {
    int client_socket = 0;
    int error = 0;
    struct sockaddr_in client = {
                                    0
                                };
    char message[LOGSZ];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (client_socket < 0 ) {
        logger2(LOGGING_FATAL, DEBUG_SOCKETS , "IPC: Failed to create socket.\n");
        exit(1);
    }

    client.sin_family = AF_INET;
    client.sin_port = htons(port);
    client.sin_addr.s_addr = serverip;

    error = connect(client_socket, (struct sockaddr *)&client, sizeof(client));

    if (error < 0) {
        sprintf(message, "IPC: Failed to connect to remote host.\n");
        logger2(LOGGING_WARN, DEBUG_SOCKETS, message);
        close(client_socket);
        return -1;
    }

    return client_socket;
}

int new_ip_client(__u32 serverip ,int port) {
    int client_socket = 0;
    int error = 0;
    struct sockaddr_in client = {
                                    0
                                };
    char message[LOGSZ];

    client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket < 0 ) {
        logger2(LOGGING_FATAL, DEBUG_SOCKETS , "IPC: Failed to create socket.\n");
        exit(1);
    }

    client.sin_family = AF_INET;
    client.sin_port = htons(port);
    client.sin_addr.s_addr = serverip;

    error = connect(client_socket, (struct sockaddr *)&client, sizeof(client));

    if (error < 0) {
        sprintf(message, "IPC: Failed to connect to remote host.\n");
        logger2(LOGGING_WARN, DEBUG_SOCKETS, message);
        close(client_socket);
        return -1;
    }

    return client_socket;
}
/*
 * Create an IP socket for the IPC.
 */
int new_ip_server(int port) {
    int server_socket = 0;
    int error = 0;
    int reusesocket = 1;
    struct sockaddr_in server = {
                                    0
                                };
    char message[LOGSZ] = { 0 };

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0 ) {
        sprintf(message, "IPC: Failed to create socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    error = setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reusesocket,sizeof(reusesocket));

    if (error < 0) {
        sprintf(message, "IPC: Failed to set socket reuse option.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    error = bind(server_socket, (struct sockaddr *)&server, sizeof(server));

    if (error < 0) {
        sprintf(message, "IPC: Failed to bind socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    error = listen(server_socket, SOMAXCONN);

    if (error == -1) {
        sprintf(message, "IPC: Could not listen on socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    logger2(LOGGING_INFO,DEBUG_SOCKETS, "[socket] Created new IP server.\n");

    return server_socket;
}

int accept_ip_client(int server_socket) {
    int newclient_socket = 0;
    int error = 0;
    struct sockaddr_in client = {
                                    0
                                };
    socklen_t in_len = 0;
    char hbuf[NI_MAXHOST] = {0};
    char sbuf[NI_MAXSERV] = {0};
    char message[LOGSZ] = { 0 };

    in_len = sizeof client;

    newclient_socket = accept(server_socket, (struct sockaddr *)&client, &in_len );

    if (newclient_socket == -1) {

        if ((errno == EAGAIN) ||
                (errno == EWOULDBLOCK)) {
            /*
             * We have processed all incoming connections.
             */
            return -1;

        } else {
            sprintf(message, "IPC: Failed to accept socket.\n");
            logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
            return -1;
        }
    }

    error = getnameinfo((struct sockaddr*)&client, sizeof client,
                        hbuf, sizeof hbuf,
                        sbuf, sizeof sbuf,
                        NI_NUMERICHOST | NI_NUMERICSERV);

    if (error == 0) {
        sprintf(message,"IPC: Accepted connection on descriptor %d "
                "(host=%s, port=%s)\n", newclient_socket, hbuf, sbuf);
        logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
    }

    return newclient_socket;
}

int new_unix_client(char* path) {
    int client_socket = 0;
    int error = 0;
    struct sockaddr_un client = {
                                    0
                                };
    char message[LOGSZ];

    client_socket = socket(AF_UNIX, SOCK_STREAM, 0);

    if (client_socket < 0 ) {
        sprintf(message, "IPC: Failed to create socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    client.sun_family = AF_UNIX;
    strncpy(client.sun_path, path,sizeof(client.sun_path)-1);
    unlink(client.sun_path);

    error = connect(client_socket, (struct sockaddr *)&client, sizeof(client));

    if (error < 0) {
        sprintf(message, "IPC: Failed to connect.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    return client_socket;
}


/*
 * Creates a new UNIX domain server socket.
 * http://troydhanson.github.io/misc/Unix_domain_sockets.html
 */
int new_unix_server(char* path) {
    int server_socket = 0;
    int error = 0;
    struct sockaddr_un server = {
                                    0
                                };
    char message[LOGSZ];

    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

    if (server_socket < 0 ) {
        sprintf(message, "IPC: Failed to create socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, path,sizeof(server.sun_path)-1);
    unlink(server.sun_path);

    error = bind(server_socket, (struct sockaddr *)&server, sizeof(server));

    if (error < 0) {
        sprintf(message, "IPC: Failed to bind socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    error = listen(server_socket, SOMAXCONN);

    if (error == -1) {
        sprintf(message, "IPC: Could not listen on socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    return server_socket;
}

int accept_unix_client(int server_socket) {
    int newclient_socket = 0;
    int error = 0;
    struct sockaddr_un client = {
                                    0
                                };
    socklen_t in_len = 0;
    char message[LOGSZ] = { 0 };

    in_len = sizeof client;

    newclient_socket = accept(server_socket, (struct sockaddr *)&client, &in_len );

    if (newclient_socket == -1) {

        if ((errno == EAGAIN) ||
                (errno == EWOULDBLOCK)) {
            /*
             * We have processed all incoming connections.
             */
            return -1;

        } else {
            sprintf(message, "IPC: Failed to accept socket.\n");
            logger(LOG_INFO, message);
            return -1;
        }
    }

    return newclient_socket;
}

int make_socket_non_blocking (int socket) {
    int flags, s;
    char message[LOGSZ];

    flags = fcntl (socket, F_GETFL, 0);
    if (flags == -1) {
        sprintf(message, "IPC: Failed getting socket flags.\n");
        logger(LOG_INFO, message);
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(socket, F_SETFL, flags);
    if (s == -1) {
        sprintf(message, "IPC: Failed setting socket flags.\n");
        logger(LOG_INFO, message);
        return -1;
    }

    return 0;
}

int register_socket(int listener_socket, int epoll_fd, struct epoll_event *event) {
    int error = 0;
    char message[LOGSZ] = {0};

    if (listener_socket < 0) {
        sprintf(message, "IPC: Failed to get socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    error = make_socket_non_blocking(listener_socket);

    if (error == -1) {
        sprintf(message, "IPC: Failed setting socket.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    event->data.fd = listener_socket;
    event->events = EPOLLIN | EPOLLET;
    error = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_socket, event);

    if (error == -1) {
        switch (errno) {
        case EBADF:
            sprintf(message, "EBADF  epfd or fd is not a valid file descriptor.\n");
            break;
        case EEXIST:
            sprintf(message, "EEXIST op was EPOLL_CTL_ADD, and the supplied file descriptor fd is\n \
                    already registered with this epoll instance.\n");
            break;
        case EINVAL:
            sprintf(message, "EINVAL epfd is not an epoll file descriptor, or fd is the same as\n \
                    epfd, or the requested operation op is not supported by this\n \
                    interface.\n");
            break;
        case ENOENT:
            sprintf(message, "ENOENT op was EPOLL_CTL_MOD or EPOLL_CTL_DEL, and fd is not\n \
                    registered with this epoll instance.\n");
            break;
        case ENOMEM:
            sprintf(message, "ENOMEM There was insufficient memory to handle the requested op\n \
                    control operation.\n");
            break;
        case ENOSPC:
            sprintf(message, "ENOSPC The limit imposed by /proc/sys/fs/epoll/max_user_watches was\n \
                    encountered while trying to register (EPOLL_CTL_ADD) a new\n \
                    file descriptor on an epoll instance.  See epoll(7) for\n \
                    further details.\n");
            break;
        case EPERM:
            sprintf(message, "EPERM  The target file fd does not support epoll.\n");
            break;
        default:
            break;
        }
        logger(LOG_INFO, message);
        sprintf(message, "IPC: Failed adding remote listener to epoll instance.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    return error;
}


int epoll_handler(struct epoller *server) {
    int client_socket;
    int error = 0;
    int numevents = 0;
    int done = 0;
    int i = 0;
    ssize_t count;
    char message[LOGSZ] = {0};
    char buf[IPC_MAX_MESSAGE_SIZE] = {0};

    /*
     * Third we listen for events and handle them.
     */
    while(1) {
        numevents = epoll_wait(server->epoll_fd, server->events, MAXEVENTS, server->timeout);

        sprintf(message, "[epoll]: processing events.\n");
        logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);

        /**
         * @todo:
         * Moved to top because it was not being executed due to "break;" in this function.
         * This could be artificially increasing traffic by making both systems send & receive
         * hello messages.  There could be a better way to do this by just handling events.
         * I need both to be sending hello message for testing though.
         */
        if(server->timeoutfunction != NULL) {
        	sprintf(message, "[epoll]: calling timeout function!\n");
        	logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
            (server->timeoutfunction(server));
        }

        for (i = 0; i < numevents; i++) {

            if ((server->events[i].events & EPOLLERR) ||
                    (server->events[i].events & EPOLLHUP) ||
                    (!(server->events[i].events & EPOLLIN))) {
                /*
                 * An error has occurred on this fd, or the socket is not
                 * ready for reading (why were we notified then?)
                 */

                sprintf(message, "[epoll]: error.\n");
                logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);

                close(server->events[i].data.fd);

                continue;

            } else if (server->events[i].data.fd == server->socket) {
                /*
                 * We have a notification on the listening socket,
                 * which means one or more incoming connections.
                 */
                while (1) {
                    /**
                     *@todo: Here we need to detect if the "server" was UNIX or IP.
                     *@todo: We might use the family type and add it to "struct epoller"
                     *@todo: and set it when we create the new epoll server.
                     *@todo: It would probably be better to check the server->socket.
                     */
                    client_socket = accept_ip_client(server->socket);

                    if (client_socket == -1) {
                        break;
                    }

                    error = make_socket_non_blocking(client_socket);

                    if (error == -1) {
                        sprintf(message, "[epoll]: Failed setting socket on client.\n");
                        logger(LOG_INFO, message);
                        exit(1);
                    }

                    if(server->secure != NULL) {
                        error = (server->secure(server, client_socket, NULL));

                        if(error == 1) { // Error == 1 passed security check.
                            error = register_socket(client_socket, server->epoll_fd, &server->event);
                        } else {
                            close(client_socket);
                        }
                    } else {
                        error = register_socket(client_socket, server->epoll_fd, &server->event);
                    }




                    continue;
                }

            }  else {
                /*
                 * We have data on the fd waiting to be read. Read and
                 * display it. We must read whatever data is available
                    * completely, as we are running in edge-triggered mode
                    * and won't get a notification again for the same
                    * data.
                 */

                done = 0;  //Need to reset this for each message.
                while(1) {

                    /**
                     * @todo:
                     * I am unsure if this is the best way to handle reading the data.
                     * It might be better to just have the callback function accept the socket FD
                     * as a param and recv() the data itself but having all data validation
                     * in one spot is kind of nice too.
                     */
                    count = recv(server->events[i].data.fd, buf, IPC_MAX_MESSAGE_SIZE, 0);

                    if(count > 0) {
                        /**
                         *@todo: Need to dynamically allocate a buffer for each epoll server.
                         *
                         * We should not overwrite the last char as NUL.  This is not a char string.
                         */
                        //buf[count - 1] = '\0';
                        sprintf(message, "[epoll]: received %u bytes.\n", (unsigned int)count);
                        logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
                    }

                    if (count == -1) {
                        /* If errno == EAGAIN, that means we have read all
                           data. So go back to the main loop. */
                        if (errno != EAGAIN) {
                            sprintf(message, "[epoll]: Failed reading message.\n");
                            logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        /* End of file. The remote has closed the
                           connection. */
                        sprintf(message, "[epoll]: Remote closed the connection.\n");
                        logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);
                        done = 1;
                        break;
                    }
                }

                (server->callback(server, server->events[i].data.fd, &buf));

                /*
                 * If the remote end is done sending data let close the connection.
                 */
                if (done) {
                    sprintf(message, "[epoll]: Closed connection on descriptor %d\n",
                            server->events[i].data.fd);
                    logger2(LOGGING_DEBUG, DEBUG_SOCKETS, message);

                    /*
                     * Closing the descriptor will make epoll remove it
                     * from the set of descriptors which are monitored.
                     */
                    close(server->events[i].data.fd);
                }
            }
        }

        /**
         * @todo:
         * If the epoll instance has a timeout and a timeout callback function it is executed.
         * This also is executed each time the epoll instance returns.
         */
        //if(server->timeoutfunction != NULL) {
        //    (server->timeoutfunction(server));
        //}

    }
    return 0;
}

/** @brief Create a new epoller instance.
 *
 * Creates a new IP based epoller instance.
 *
 * @param epoller [in] pinter to the epoller.
 * @param secure [in] function pointer that will validate source.
 * @param callback [in] function pointer that will process client messages.
 * @param port [in] port to server on.
 * @param timeoutfunction [in] function that will run when epoller instance timeout is reached.
 * @param timeout [in] number of seconds before epoller instance will timeout.
 *
 * @toto:
 * A new epoll server should include a max length for the messages it is expected to receive.
 * If it receives a message larger than this is should shutdown that socket right away.
 */
int new_ip_epoll_server(struct epoller *server, t_epoll_callback secure, t_epoll_callback callback, int port, t_epoll_timeout timeoutfunction, int timeout) {
    char message[LOGSZ] = {0};

    server->events = calloc (MAXEVENTS, sizeof server->event);

    if(server->events == NULL) {
        exit(1);
    }

    server->epoll_fd = epoll_create1(0);

    if(server->epoll_fd == -1) {
        sprintf(message, "IPC: Could not create epoll instance.\n");
        logger(LOG_INFO, message);
        exit(1);
    }

    /*
     * First we setup the remote IPC listener socket.
     * This accepts connections from the remote neighbors.
     */

    if(port != 0){
    	server->socket = new_ip_server(port);
    }
    server->timeoutfunction = timeoutfunction;
    server->timeout = timeout;

    if(server->socket != NULL){
    	register_socket(server->socket, server->epoll_fd, &server->event);
    }

    server->secure = secure;
    server->callback = callback;

    return 0;
}


int shutdown_epoll_server(struct epoller *server) {
    free(server->events);
    close(server->socket);
    return 0;
}
