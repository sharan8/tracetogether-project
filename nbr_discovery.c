#include "contiki.h"
#include "dev/leds.h"
#include <stdio.h>
#include <stdlib.h>
#include "core/net/rime/rime.h"
#include "dev/serial-line.h"
#include "dev/uart1.h"
#include "node-id.h"
#include "defs_and_types.h"
#include "net/netstack.h"
#include "random.h"
#include "lib/memb.h"
#ifdef TMOTE_SKY
#include "powertrace.h"
#endif
/*---------------------------------------------------------------------------*/
#define TOTAL_SLOTS 25
#define SLOT_TIME (RTIMER_SECOND/64)
#define PROBE_SLOTS (TOTAL_SLOTS/2)
#define MAX_ITEMS 20 // for hashtable
#define SIZE 20 // for hashtable
#define RSSI_THRESHOLD -63 // for entry into 3m radius
#define MAINTENANCE_FREQ 3  // maintenance per number of anchor slots
/*---------------------------------------------------------------------------*/
// duty cycle = WAKE_TIME / (WAKE_TIME + SLEEP_SLOT * SLEEP_CYCLE)
/*---------------------------------------------------------------------------*/
// sender timer
static struct rtimer rt;
static struct pt pt;
/*---------------------------------------------------------------------------*/
static data_packet_struct received_packet;
static data_packet_struct data_packet;
unsigned long curr_timestamp;
/*---------------------------------------------------------------------------*/
static int curr_slot_index = 1;
static int permutation_arr_index = 0;
static int permutation_arr[PROBE_SLOTS];
static int debug = 0;
/*---------------------------------------------------------------------------*/
// Get a random permutation for the active probing slots
void populate_permuation_arr();
/*---------------------------------------------------------------------------*/
PROCESS(cc2650_nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&cc2650_nbr_discovery_process);
/*---------------------------------------------------------------------------*/
// HASHMAP IMPLEMENTATION

// Represents an encountered node
struct TrackedNode {
  int node_id;
  unsigned long last_seen;     // last seen timestamp
  unsigned long window_expiry; // end of 30s window for node
};

struct TrackedNode* hashArray[SIZE]; // hashtable
struct TrackedNode* dummyItem;
struct TrackedNode* item;

int hashCode(int key) {
  return key % SIZE;
}

struct TrackedNode *search(int node_id) {
  //get the hash 
  int hashIndex = hashCode(node_id);  

  //move in array until an empty 
  while(hashArray[hashIndex] != NULL) {

    if(hashArray[hashIndex]->node_id == node_id)
      return hashArray[hashIndex]; 

    //go to next cell
    ++hashIndex;

    //wrap around the table
    hashIndex %= SIZE;
  }        

  return NULL;        
}

void insert(int node_id, uint16_t arrival_time, uint16_t last_seen) {
  MEMB(dummy_mem, struct TrackedNode, MAX_ITEMS);
  struct TrackedNode *item;
  item = memb_alloc(&dummy_mem);
  item->node_id = node_id;
  item->last_seen = last_seen;
  item->window_expiry = arrival_time + 30;

  //get the hash 
  int hashIndex = hashCode(node_id);

  //move in array until an empty or deleted cell
  while(hashArray[hashIndex] != NULL && hashArray[hashIndex]->node_id != -1) {
    //go to next cell
    ++hashIndex;

    //wrap around the table
    hashIndex %= SIZE;
  }

  hashArray[hashIndex] = item;
  printf("Successfully added node with node ID: %d\n", node_id);
}

struct TrackedNode* delete(struct TrackedNode* item) {
  int node_id = item->node_id;

  //get the hash 
  int hashIndex = hashCode(node_id);

  //move in array until an empty
  while(hashArray[hashIndex] != NULL) {

    if(hashArray[hashIndex]->node_id == node_id) {
      struct TrackedNode* temp = hashArray[hashIndex]; 

      //assign a dummy item at deleted position
      hashArray[hashIndex] = dummyItem; 
      return temp;
    }

    //go to next cell
    ++hashIndex;

    //wrap around the table
    hashIndex %= SIZE;
  }      

  return NULL;        
}

void display() {
  int i = 0;

  for(i = 0; i<SIZE; i++) {

    if(hashArray[i] != NULL)
      printf(" (%d,%d,%d)",hashArray[i]->node_id,hashArray[i]->window_expiry,hashArray[i]->last_seen);
    else
      printf(" ~~ ");
  }

  printf("\n");
}

static void test_hashmap() {
  MEMB(dummy_mem, struct TrackedNode, MAX_ITEMS);
  struct TrackedNode *dummyItem;
  dummyItem = memb_alloc(&dummy_mem);
  dummyItem->node_id = -1;
  dummyItem->last_seen = -1;
  dummyItem->window_expiry = -1;

  insert(1, 20, 20);
  insert(2, 70, 20);
  insert(3, 80, 80);

  display();
  item = search(3);

  if(item != NULL) {
    printf("Node found with ID: %d and last seen: %lu and expiry: %lu\n", item->node_id, item->last_seen, item->window_expiry);
  } else {
    printf("Node not found\n");
  }

  delete(item);
  item = search(3);

  if(item != NULL) {
    printf("Node found with ID: %d\n", item->node_id);
  } else {
    printf("Node not found\n");
  }
}

/*---------------------------------------------------------------------------*/
  static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  memcpy(&received_packet, packetbuf_dataptr(), sizeof(data_packet_struct));
  if (debug) {
    // printf("Send seq# %lu  @ %8lu  %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
    printf("Rcv pkt RSSI: %d\n", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI));
  }
  printf("%3lu.%03lu DETECT %lu\n", received_packet.timestamp / CLOCK_SECOND, received_packet.src_id);
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
char sender_scheduler(struct rtimer *t, void *ptr) {
  static uint16_t i = 0;
  static int NumSleep=0;
  PT_BEGIN(&pt);

  if (debug) {
    curr_timestamp = clock_time(); 
    printf("Start clock %lu ticks, timestamp %3lu.%03lu\n", curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
  }

  while(1){

    // radio on
    NETSTACK_RADIO.on();

    for(i = 0; i < NUM_SEND; i++){
      data_packet.seq++;
      curr_timestamp = clock_time();
      data_packet.timestamp = curr_timestamp;

      if (debug) {
        printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
      }
      packetbuf_copyfrom(&data_packet, (int)sizeof(data_packet_struct));
      broadcast_send(&broadcast);

      if(i != (NUM_SEND - 1)){
        rtimer_set(t, RTIMER_TIME(t) + SLOT_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
        PT_YIELD(&pt);
      }
    }

    // radio off
    NETSTACK_RADIO.off();

    int nxt_slot_index;
    // the node is awake either during the anchor slot or the active probing slot, 
    if (curr_slot_index != 1) {
      nxt_slot_index = 1;
    } else {
      nxt_slot_index = permutation_arr[permutation_arr_index];
      permutation_arr_index = (permutation_arr_index + 1) % (PROBE_SLOTS);
    }

    int slot_diff = nxt_slot_index - curr_slot_index - 1;
    NumSleep = (slot_diff >= 0) ? slot_diff : (TOTAL_SLOTS + slot_diff);  
    if (debug) {
      printf(" Sleep for %d slots \n", NumSleep);
    }
    curr_slot_index = nxt_slot_index;

    // NumSleep should be a constant or static int
    for(i = 0; i < NumSleep; i++){
      rtimer_set(t, RTIMER_TIME(t) + SLOT_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
      PT_YIELD(&pt);
    }
  }

  PT_END(&pt);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(cc2650_nbr_discovery_process, ev, data)
{
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  random_init(54222 + node_id);

  // get probing permuation
  populate_permuation_arr(); 

#ifdef TMOTE_SKY
  powertrace_start(CLOCK_SECOND * 5);
#endif

  broadcast_open(&broadcast, 129, &broadcast_call);

  // for serial port
#if !WITH_UIP && !WITH_UIP6
  uart1_set_input(serial_line_input_byte);
  serial_line_init();
#endif

  printf("CC2650 neighbour discovery\n");
  printf("Node %d will be sending packet of size %d Bytes\n", node_id, (int)sizeof(data_packet_struct));

  // radio off
  NETSTACK_RADIO.off();

  // initialize data packet
  data_packet.src_id = node_id;
  data_packet.seq = 0;

  // Start sender in one millisecond.
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void populate_permuation_arr() {
  int random_index, i;
  for (i = 2; i <= PROBE_SLOTS + 1; i++) {
    do {
      random_index = random_rand() % PROBE_SLOTS;
    } while (permutation_arr[random_index] != 0);
    permutation_arr[random_index] = i;
  }

  if (debug) {
    // print out the active probe permutation
    printf("Permutation is");
    for (i = 0; i < PROBE_SLOTS; i++) {
      printf(" %d", permutation_arr[i]);
    }
    printf("\n");
  }
}
/*---------------------------------------------------------------------------*/
