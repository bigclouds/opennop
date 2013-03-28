#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> // for multi-threading
#include <stdbool.h>

#include <sys/types.h>
#include <linux/types.h>

#include "memorymanager.h"
#include "opennopd.h"
#include "logger.h"

u_int32_t allocatedpacketbuffers;
struct packet_head freepacketbuffers;
pthread_cond_t mysignal; // Condition signal used to wake-up thread.
pthread_mutex_t mylock; // Lock for the memorymanager.

int initialfreepacketbuffers = 1000;
int minfreepacketbuffers = 500;
int packetbufferstoallocate = 100;

int DEBUG_MEMORYMANAGER = true;

void *memorymanager_function(void *dummyPtr) {
	struct packet_head packetbufferstaging;
	u_int32_t newpacketbuffers;
	char message[LOGSZ];

	if (DEBUG_MEMORYMANAGER == true) {
		sprintf(message, "[OpenNOP]: Starting memory manager thread. \n");
		logger(LOG_INFO, message);
	}

	/*
	 * Initialize the memory manager lock and signal.
	 */
	pthread_cond_init(&mysignal, NULL);
	pthread_mutex_init(&mylock, NULL);
	pthread_mutex_lock(&mylock);
	allocatedpacketbuffers = 0;
	pthread_mutex_unlock(&mylock);

	/*
	 * Initialize the free packet buffer pool lock and signal.
	 */
	pthread_mutex_lock(&freepacketbuffers.lock);
	freepacketbuffers.qlen = 0;
	pthread_mutex_unlock(&freepacketbuffers.lock);

	/*
	 * Initialize the staging packet buffer queue lock and signal.
	 */
	pthread_mutex_lock(&packetbufferstaging.lock);
	packetbufferstaging.qlen = 0;
	pthread_mutex_unlock(&packetbufferstaging.lock);

	/*
	 * I need to initialize some packet buffers here.
	 * and move them to the freepacketbuffers pool.
	 */
	allocatefreepacketbuffers(&packetbufferstaging, initialfreepacketbuffers);
	allocatedpacketbuffers += movepacketbuffers(&packetbufferstaging,
			&freepacketbuffers);

	while (servicestate >= STOPPING) {

		/*
		 * Check if there are enough buffers.  If so then sleep.
		 */
		pthread_mutex_lock(&freepacketbuffers.lock); // Grab the free packet buffer pool lock.

		if (freepacketbuffers.qlen >= minfreepacketbuffers) {
			pthread_mutex_unlock(&freepacketbuffers.lock); // Lose the free packet buffer pool lock.
			pthread_mutex_lock(&mylock); // Grab lock.
			pthread_cond_wait(&mysignal, &mylock); // If we have enough free buffers then wait.
		} else {
			pthread_mutex_unlock(&freepacketbuffers.lock); // Lose the free packet buffer pool lock.
		}

		/*
		 * Something woke me up.  We allocate packet buffers now!
		 * Then move them to the freepacketbuffers pool.
		 */
		pthread_mutex_unlock(&mylock); // Lose lock while staging new buffers.
		allocatefreepacketbuffers(&packetbufferstaging, packetbufferstoallocate);

		pthread_mutex_lock(&mylock); // Grab lock again before modifying free packet buffer pool.
		newpacketbuffers = movepacketbuffers(&packetbufferstaging,
				&freepacketbuffers);
		allocatedpacketbuffers += newpacketbuffers;

		if (DEBUG_MEMORYMANAGER == true) {
			sprintf(message, "[OpenNOP]: Allocating %u new packet buffers. \n",
					newpacketbuffers);
			logger(LOG_INFO, message);
		}

		/*
		 * Ok finished allocating more packet buffers.  Lets do it again.
		 */
		pthread_mutex_unlock(&mylock); // Lose lock.

	}

	if (DEBUG_MEMORYMANAGER == true) {
		sprintf(message, "[OpenNOP]: Stopping memory manager thread. \n");
		logger(LOG_INFO, message);
	}
	/*
	 * We need to do memory cleanup here.
	 */

	/* Thread ending */
	return NULL;
}

/*
 * This function allocates a number of free packets
 * and stores them in the specified queue.
 */
int allocatefreepacketbuffers(struct packet_head *queue, int bufferstoallocate) {
	int i;

	for (i = 0; i < bufferstoallocate; i++) {
		queue_packet(queue, newpacket());
	}

	return 0;
}

/*
 * This function moves all packet buffers from one queue to another.
 * Returns how many were moved.
 */
u_int32_t movepacketbuffers(struct packet_head *fromqueue,
		struct packet_head *toqueue) {
	struct packet *start; // Points to the first packet of the list.
	struct packet *end; // Points to the last packet of the list.
	u_int32_t qlen; // How many buffers are being moved.

	/*
	 * Get the first and last buffers in the source queue
	 * and remove all the packet buffers from the source queue.
	 */
	pthread_mutex_lock(&fromqueue->lock);
	start = fromqueue->next;
	end = fromqueue->prev;
	qlen = fromqueue->qlen;
	fromqueue->next = NULL;
	fromqueue->prev = NULL;
	fromqueue->qlen = 0;
	pthread_mutex_unlock(&fromqueue->lock);

	/*
	 * Add those buffers to the destination queue.
	 */
	pthread_mutex_lock(&toqueue->lock);
	if (toqueue->next == NULL) {
		toqueue->next = start;
		toqueue->prev = end;
		toqueue->qlen = qlen;
	} else {
		start->prev = toqueue->prev;
		toqueue->prev->next = start;
		toqueue->prev = end;
		toqueue->qlen += qlen;
	}
	pthread_mutex_unlock(&toqueue->lock);

	return qlen;
}

struct packet *get_freepacket_buffer(void) {
	struct packet *thispacket = NULL;

	/*
	 * Check if any packet buffers are in the pool
	 * get one if there are or allocate a new buffer if not.
	 */
	pthread_mutex_lock(&freepacketbuffers.lock); // Grab packet buffer pool lock.

	if (freepacketbuffers.qlen > 0) {

		if (freepacketbuffers.qlen < minfreepacketbuffers) {
			pthread_cond_signal(&mysignal); // Free packet buffers are low!
		}
		pthread_mutex_unlock(&freepacketbuffers.lock); // Lose packet buffer pool lock.
		thispacket = dequeue_packet(&freepacketbuffers, false); // This uses its own lock.
	} else {
		pthread_mutex_unlock(&freepacketbuffers.lock); // Lose packet buffer pool lock.
		pthread_cond_signal(&mysignal); // Free packet buffers are low!
		thispacket = newpacket();
		pthread_mutex_lock(&mylock); // Grab lock.
		allocatedpacketbuffers++;
		pthread_mutex_unlock(&mylock); // Lose lock.
	}

	return thispacket;
}

int put_freepacket_buffer(struct packet *thispacket) {
	int result;
	char message[LOGSZ];

	if (DEBUG_MEMORYMANAGER == true) {
		sprintf(message, "[OpenNOP]: Returning a packet buffer to the pool. \n");
		logger(LOG_INFO, message);
	}
	result = queue_packet(&freepacketbuffers, thispacket);

	if (DEBUG_MEMORYMANAGER == true) {

		if (result < 0) {
			sprintf(message,
					"[OpenNOP]: Return packet buffer to the pool failed! \n");
		} else {
			sprintf(message,
					"[OpenNOP]: Returned packet buffer to the pool. \n");
		}
		logger(LOG_INFO, message);
	}
	return result;
}

