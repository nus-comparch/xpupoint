#include <stdint.h>

static __managed__ uint64_t total_dynamic_instr_counter = 0;
static __managed__ uint64_t reported_dynamic_instr_counter = 0;
static __managed__ bool stop_report = false;

/* information collected in the instrumentation function and passed
 * on the channel from the GPU to the CPU */

typedef struct {
  int cta_id_x;
  int cta_id_y;
  int cta_id_z;
	int warpid_tb;
	int warpid_sm;
  int sm_id;
  int opcode_id;
  uint32_t vpc;
  uint32_t active_mask;
  uint32_t predicate_mask;
  uint32_t num_insn;
	int thread_id;
} inst_trace_t;
