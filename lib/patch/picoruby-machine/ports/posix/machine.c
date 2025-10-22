#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include "../../include/machine.h"


void
Machine_tud_task(void)
{
}

bool
Machine_tud_mounted_q(void)
{
  return true;
}

void
Machine_delay_ms(uint32_t ms)
{
    usleep(ms *1000);
}

void
Machine_busy_wait_ms(uint32_t ms)
{
}

void
Machine_sleep(uint32_t seconds)
{
    usleep(seconds*1000*1000);
}

bool
Machine_get_unique_id(char *id_str)
{
  FILE *fp = fopen("/etc/machine-id", "r");
  if (fp) {
    if (fgets(id_str, 32, fp) == NULL) {
      perror("Failed to read /etc/machine-id");
      fclose(fp);
      return false;
    }
    fclose(fp);
    return true;
  }
  perror("Failed to open /etc/machine-id");
  return false;
}

uint32_t
Machine_stack_usage(void)
{
  return 0;
}

const char *
Machine_mcu_name(void)
{
  return "POSIX";
}

void
Machine_exit(int status)
{
  sigint_status = MACHINE_SIGINT_EXIT;
  exit_status = status;
  raise(SIGINT);
}


int Machine_get_config_int(int type)
{
  switch(type)
  {
    case 0:
    return MRB_TICK_UNIT;
    case 1:
    return MRB_TIMESLICE_TICK_COUNT;
  }
  return 0;
}
