/* Requirements: ORT-INT-001 */
#include <dds/dds.h>
#include <stdio.h>

int main(void) {
  const dds_entity_t participant = dds_create_participant(43, NULL, NULL);
  if (participant < 0) {
    fprintf(stderr, "Cyclone participant: %s\n", dds_strretcode(-participant));
    return 1;
  }
  dds_sleepfor(DDS_SECS(5));
  return dds_delete(participant) == DDS_RETCODE_OK ? 0 : 1;
}
