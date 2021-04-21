#include "contiki.h"
#include "dev/leds.h"
#include <stdio.h>
#include "core/net/rime/rime.h"
#include "dev/serial-line.h"
#include "dev/uart1.h"
#include "node-id.h"
#include "defs_and_types.h"
#include "net/netstack.h"
#include "random.h"
#ifdef TMOTE_SKY
#include "powertrace.h"
#endif
/*---------------------------------------------------------------------------*/
//Parameters for BlindDate Protocol
#define PERIOD 0.1 //period where static anchor slot repeats
#define HYPERPERIOD 1 //period where scheduling pattern repeats over
#define M 5 //number of blocks in a period
#define S 3 //number of timeslots in a block
#define NSLOTS 15 //total number of timeslots in a period
#define K 1 //number of dynamic probes, K cannot be more than M
#define STEP 1 //number of minislots to be increased/decrease evrey PERIOD
#define SLOT_TIME RTIMER_SECOND/50 //~20ms
static short debug = 0; //0 to turn off debug statements
static short current_slot = 0;
static short anchor_slot = NSLOTS - 1;
static short probe_slot_1 = 0; //forward probe
static short probe_slot_2 = 0; //backward probe
static short probe_1_flag = 1; //1 if probe has not been active in current period, 0 if it has
static short probe_2_flag = 1;
// sender timer
static struct rtimer rt;
static struct pt pt;
/*---------------------------------------------------------------------------*/
static data_packet_struct received_packet;
static data_packet_struct data_packet;
unsigned long curr_timestamp;

//return 1 if node should be active during current slot
int isActive(){
  if(current_slot == anchor_slot){
    probe_1_flag = probe_2_flag = 1; //set flag
    if(debug) printf("Node %d: Slot %d, active ANCHOR SLOT \n", node_id, current_slot);
    return 1;
  }
  if(probe_1_flag){
    if(current_slot == probe_slot_1){
      probe_1_flag = 0; //clear flag
      if((probe_slot_1 + STEP)/S > probe_slot_1/S){
        probe_slot_1 = (probe_slot_1/S) * S;
      }else{    
        probe_slot_1 = (probe_slot_1 + STEP) % NSLOTS;
      }  
      if(debug) printf("Node %d: Slot %d, active PROBE1\n", node_id, current_slot);
      return 1;
    }
  }
  if(probe_2_flag){
    if(current_slot == probe_slot_2){
      probe_2_flag = 0; //clear flag
      if(probe_slot_2 == 0){
        probe_slot_2 = S - 3;
        if(debug) printf("Node %d: Slot %d, active PROBE2\n", node_id, current_slot);
        return 1;
      }
      if((probe_slot_2 - STEP)/S < probe_slot_2/S){ // if slot == 0, 0 - 1 = -1 
        probe_slot_2 = ((probe_slot_2/S) + 1) * S - 1;
      }else{
        probe_slot_2 = (probe_slot_2 - STEP) % NSLOTS;
      } 
      if(debug) printf("Node %d: Slot %d, active PROBE2\n", node_id, current_slot);
      return 1;
    }
  }

  if(debug) printf("Node %d: Slot %d, sleep\n", node_id, current_slot);
  return 0;
}

/*---------------------------------------------------------------------------*/
PROCESS(cc2650_nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&cc2650_nbr_discovery_process);
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  leds_on(LEDS_GREEN);
  memcpy(&received_packet, packetbuf_dataptr(), sizeof(data_packet_struct));

  // printf("Send seq# %lu  @ %8lu  %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

  printf("Received packet from node %lu with sequence number %lu and timestamp %3lu.%03lu\n", received_packet.src_id, received_packet.seq, received_packet.timestamp / CLOCK_SECOND, ((received_packet.timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
  leds_off(LEDS_GREEN);
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
char sender_scheduler(struct rtimer *t, void *ptr) {
  static uint16_t i = 0;
  static int NumSleep=0;
  PT_BEGIN(&pt);

  curr_timestamp = clock_time(); 
  printf("Start clock %lu ticks, timestamp %3lu.%03lu\n", curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

  while(1){
    
    if(isActive()){
        leds_off(LEDS_BLUE);

        // radio on
        NETSTACK_RADIO.on();
        for(i = 0; i < NUM_SEND; i++){
            leds_on(LEDS_RED);
            
            data_packet.seq++;
            curr_timestamp = clock_time();
            data_packet.timestamp = curr_timestamp;

            printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

            packetbuf_copyfrom(&data_packet, (int)sizeof(data_packet_struct));
            broadcast_send(&broadcast);
            leds_off(LEDS_RED);

            if(i != (NUM_SEND - 1)){
                rtimer_set(t, RTIMER_TIME(t) + SLOT_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
                PT_YIELD(&pt);
            }
        }
    }else{
        leds_on(LEDS_BLUE);
        // radio off
        NETSTACK_RADIO.off();
        if(debug) printf("Sleeping at slot %d \n", current_slot);
        rtimer_set(t, RTIMER_TIME(t) + SLOT_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
        PT_YIELD(&pt);
        
    }
    current_slot++;
    current_slot = current_slot % NSLOTS;
  }
  
  PT_END(&pt);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(cc2650_nbr_discovery_process, ev, data)
{
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  random_init(node_id); //Node id is used as the seed to prevent both nodes from choosing same row and column

  #ifdef TMOTE_SKY
  powertrace_start(CLOCK_SECOND * 5);
  #endif

  broadcast_open(&broadcast, 129, &broadcast_call);

  // for serial port
  #if !WITH_UIP && !WITH_UIP6
  uart1_set_input(serial_line_input_byte);
  serial_line_init();
  #endif

  //randomise which unique blocks the probes will be in out of M-1 blocks 
  while (probe_slot_1 == probe_slot_2)
  {
    probe_slot_1 = random_rand() % (M-1);
    probe_slot_2 = random_rand() % (M-1);
  }
  probe_slot_1 = probe_slot_1 * S;
  probe_slot_2 = (probe_slot_2 + 1) * S - 1;
  printf("Node %d starting fwdprobe at %d and bckwardprobe at %d\n", node_id,probe_slot_1,probe_slot_2);


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
